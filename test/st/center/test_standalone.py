"""Run the release image on loopback, recreate it on the same volume and verify persisted state."""

import argparse
import json
import os
import secrets
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.request import HTTPSHandler, build_opener

from daemon_smoke import NoRedirect, request, verify_write


def docker(*args):
    return subprocess.check_output(["docker", *args], text=True).strip()


def wait_for_visualization(container, url):
    docker(
        "exec",
        container,
        "python",
        "-c",
        """
import sys
import time
from urllib.request import urlopen

deadline = time.monotonic() + 120
while True:
    try:
        with urlopen(sys.argv[1], timeout=2) as response:
            if response.status == 200:
                break
    except OSError:
        pass
    if time.monotonic() >= deadline:
        raise RuntimeError(f"Visualization health check timed out: {sys.argv[1]}")
    time.sleep(0.5)
""",
        url,
    )


def stop_visualization_process(container, executable):
    docker(
        "exec",
        container,
        "python",
        "-c",
        """
import os
import signal
import sys
from pathlib import Path

target = os.fsencode(sys.argv[1])
for path in Path("/proc").glob("[0-9]*/cmdline"):
    try:
        arguments = path.read_bytes().split(b"\\0")[:2]
    except FileNotFoundError:
        continue
    if target in arguments:
        os.kill(int(path.parent.name), signal.SIGKILL)
        break
else:
    raise RuntimeError(f"Process not found: {sys.argv[1]}")
""",
        executable,
    )


def verify_visualization_isolation(container, opener, origin, tokens):
    components = (
        ("Grafana", "/usr/share/grafana/bin/grafana", "http://127.0.0.1:3000/api/health"),
        ("summary service", "/opt/msmonitor-visualization/backend/server.py", "http://127.0.0.1:19103/health"),
    )
    for name, executable, url in components:
        wait_for_visualization(container, url)
        stop_visualization_process(container, executable)
        degraded = subprocess.run(
            ["docker", "exec", container, "python", "/app/center.py", "visualization-healthcheck"],
            capture_output=True,
            text=True,
            check=False,
        )
        component = "grafana" if name == "Grafana" else "summary"
        if degraded.returncode != 1 or json.loads(degraded.stdout).get(component) is not False:
            raise RuntimeError(f"Fault not reported for {name}: {degraded.stdout} {degraded.stderr}")
        verify_write(opener, origin, 10, tokens["write"]["10"], secrets.token_hex(8), query_token=tokens["read"]["10"])
        docker("exec", container, "python", "/app/center.py", "healthcheck")
        wait_for_visualization(container, url)
        docker("exec", container, "python", "/app/center.py", "visualization-healthcheck")
        print(f"PASS: {name} restarted and center samples written after the fault were read back")


def exercise(image, root, *, with_visualization=False):
    config_dir = root / "config"
    config_dir.mkdir(mode=0o755)
    config_dir.chmod(0o755)
    cert = config_dir / "ca.pem"
    key = root / "key.pem"
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-days",
            "1",
            "-subj",
            "/CN=localhost",
            "-addext",
            "subjectAltName=IP:127.0.0.1",
            "-keyout",
            str(key),
            "-out",
            str(cert),
        ],
        check=True,
        stderr=subprocess.DEVNULL,
    )
    pem = config_dir / "tls.pem"
    pem.write_bytes(cert.read_bytes() + key.read_bytes())
    pem.chmod(0o644)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    origin = f"https://127.0.0.1:{port}"
    tenants = ("10", "0")
    tokens = {role: {tenant: secrets.token_hex(24) for tenant in tenants} for role in ("write", "read")}
    control = secrets.token_hex(24)
    config = {
        "tenants": {k: {r + "_token": tokens[r][k] for r in tokens} for k in tenants},
        "i8_tokens": {"lab": control},
        "scope_accounts": {"lab": "10"},
        "public_write_origin": origin,
    }
    config_file = config_dir / "center.json"
    config_file.write_text(json.dumps(config))
    config_file.chmod(0o644)
    if with_visualization:
        password_file = config_dir / "grafana-admin-password"
        password_file.write_text(secrets.token_hex(24))
        password_file.chmod(0o644)
    opener = build_opener(NoRedirect(), HTTPSHandler(context=ssl.create_default_context(cafile=str(cert))))

    def post(endpoint, data, expected=200):
        return json.loads(
            request(
                opener,
                origin,
                "/api/v1/daemon/" + endpoint,
                token=control,
                body=json.dumps(data).encode(),
                expected=expected,
                headers={"Content-Type": "application/json"},
            )
        )

    run_id = secrets.token_hex(8)
    volume = docker("volume", "create", "msmonitor-st-" + run_id)
    container = None
    try:
        for attempt in range(2):
            container = docker(
                "run",
                "-d",
                "-p",
                f"127.0.0.1:{port}:8443",
                "-v",
                volume + ":/data",
                "-v",
                str(config_dir) + ":/etc/msmonitor:ro",
                *(("-e", "MSMONITOR_GRAFANA_TENANT=10") if with_visualization else ()),
                image,
            )
            deadline = time.monotonic() + 180
            while docker("inspect", "-f", "{{.State.Health.Status}}", container) != "healthy":
                if time.monotonic() >= deadline or docker("inspect", "-f", "{{.State.Running}}", container) != "true":
                    raise RuntimeError(docker("logs", "--tail", "40", container))
                time.sleep(2)
            registration = post("register", {"daemon_id": run_id, "instance_id": "boot1"})
            assert registration["remote_write_url"] == origin + "/insert/10/prometheus/api/v1/write"
            if attempt == 0:
                session = registration["session_id"]
                batch = {"batch_id": run_id, "session_id": session, "schema_version": 1, "records": [{"value": 1}]}
                assert post("submit-summary", batch)["persisted"]
                verify_write(opener, origin, 10, tokens["write"]["10"], run_id, query_token=tokens["read"]["10"])
                query_time = int(time.time()) + 1
                for role in tokens:
                    (root / role).write_text(json.dumps(tokens[role]))
                subprocess.run(
                    [
                        sys.executable,
                        str(Path(__file__).with_name("test_tenant_queries.py")),
                        "--origin",
                        origin,
                        "--ca",
                        str(cert),
                        "--write-tokens",
                        str(root / "write"),
                        "--query-tokens",
                        str(root / "read"),
                        "--tenants",
                        *tenants,
                    ],
                    check=True,
                )
                if with_visualization:
                    verify_visualization_isolation(container, opener, origin, tokens)
            else:
                assert registration["session_id"] == session
                post("submit-summary", {**batch, "records": [{"value": 2}]}, expected=409)
                assert post("heartbeat", {"session_id": session, "status_seq": 1, "collect_state": "running"})[
                    "accepted"
                ]
                from urllib.parse import urlencode

                query = urlencode(
                    {
                        "query": f'last_over_time(msmonitor_smoke_alert{{run_id="{run_id}"}}[5m])',
                        "time": query_time,
                        "latency_offset": "1ms",
                        "nocache": "1",
                    }
                )
                rows = json.loads(
                    request(opener, origin, "/select/10/prometheus/api/v1/query?" + query, token=tokens["read"]["10"])
                )["data"]["result"]
                assert len(rows) == 1 and float(rows[0]["value"][1]) == 3
            docker("stop", "-t", "45", container)
            docker("rm", container)
            container = None
        print("PASS: release image HTTPS, tenant queries, scope discovery, session, summary and samples after recreate")
    finally:
        if container:
            docker("rm", "-f", container)
        docker("volume", "rm", volume)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument(
        "--with-visualization", action="store_true", help="Verify recovery from Grafana process failures"
    )
    options = parser.parse_args()
    os.umask(0o077)
    with tempfile.TemporaryDirectory(dir=Path.home(), prefix=".msmonitor-standalone-") as folder:
        exercise(options.image, Path(folder), with_visualization=options.with_visualization)
