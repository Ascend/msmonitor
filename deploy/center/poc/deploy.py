"""Deploy the center ingestion POC on a Linux Docker host."""

import argparse
import json
import os
import re
import secrets
import sys
import shutil
import subprocess  # nosec B404 - Docker runs via an argument vector, without a shell.
from pathlib import Path


PREFIX = "req2-final"
PORT = 18467
VM = "v1.155.0"
WRITERS = ("insert1", "insert2")
TENANTS = (10, 20)
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))
from center_config import auth_config  # noqa: E402


def docker(*args):
    executable = shutil.which("docker")
    if executable is None:
        raise RuntimeError("Docker CLI is required")
    command = [str(Path(executable).resolve()), *args]
    # The Docker CLI path is resolved above. Arguments come from this deployment.
    return subprocess.check_output(command, text=True).strip()  # nosec B603


def inspect(name):
    return json.loads(docker("inspect", name))[0]


def configure_auth(work, write_backend, query_backend):
    tokens = {tenant: secrets.token_hex(24) for tenant in TENANTS}
    write_tokens = {tenant: secrets.token_hex(24) for tenant in TENANTS}
    query_tokens = {tenant: secrets.token_hex(24) for tenant in TENANTS}
    tenants = {tenant: {"write_token": write_tokens[tenant], "read_token": query_tokens[tenant]} for tenant in TENANTS}
    config = json.loads(auth_config(tenants, write_backend, query_backend))
    by_name = {user["name"]: user["url_map"] for user in config["users"]}
    for tenant in TENANTS:
        routes = by_name[f"tenant-{tenant}-write"] + by_name[f"tenant-{tenant}-read"]
        config["users"].append({"name": f"tenant-{tenant}-test", "bearer_token": tokens[tenant], "url_map": routes})
    (work / "auth.yml").write_text(json.dumps(config), encoding="utf-8")
    for filename, credentials in (
        ("tokens.json", tokens),
        ("write-tokens.json", write_tokens),
        ("query-tokens.json", query_tokens),
    ):
        (work / filename).write_text(json.dumps(credentials) + "\n", encoding="utf-8")


def deploy(work, *, prefix=PREFIX, port=PORT, stats_port=None):
    front, back = (f"{prefix}-front", f"{prefix}-back")

    def address(component):
        data = inspect(f"{prefix}-{component}")
        return data["NetworkSettings"]["Networks"][back]["IPAddress"]

    def launch(component, image, *flags, options=()):
        print(docker("run", "-d", "--name", f"{prefix}-{component}", *options, image, *flags))

    # This stack has separate names and storage. Existing req2 test evidence is kept.
    work.mkdir(mode=0o700, parents=True)
    print(docker("network", "create", front))
    print(docker("network", "create", "--internal", back))
    private = ("--network", back)
    launch("storage", f"victoriametrics/vmstorage:{VM}-cluster", "-retentionPeriod=1d", options=private)
    storage = address("storage")
    for writer in WRITERS:
        launch(writer, f"victoriametrics/vminsert:{VM}-cluster", f"-storageNode={storage}:8400", options=private)
    launch("select", f"victoriametrics/vmselect:{VM}-cluster", f"-storageNode={storage}:8401", options=private)
    config = work / "haproxy.cfg"
    config.write_text(f"""global
    maxconn 12000
defaults
    mode tcp
    timeout connect 3s
    timeout client 30s
    timeout server 30s
frontend remote_write
    bind :8480
    option contstats
    default_backend writers
backend writers
    balance roundrobin
    option httpchk
    http-check connect
    http-check send meth GET uri /-/healthy ver HTTP/1.1 hdr Host vminsert
    http-check expect status 200
    default-server inter 1s fall 2 rise 2 on-marked-down shutdown-sessions
    server insert1 {address("insert1")}:8480 check
    server insert2 {address("insert2")}:8480 check
frontend status
    mode http
    bind :8404
    monitor-uri /readyz
    monitor fail if {{ nbsrv(writers) lt 1 }}
    http-request return status 200 if {{ path /livez }}
    http-request use-service prometheus-exporter if {{ path /metrics }}
    stats enable
    stats uri /stats
""")
    config.chmod(0o644)
    l4_options = private
    if stats_port is not None:
        # Docker needs a non-internal network to publish the status port.
        stats_network = f"{prefix}-stats"
        print(docker("network", "create", stats_network))
        l4_options = ("--network", stats_network, "-p", f"127.0.0.1:{stats_port}:8404")
    launch(
        "l4",
        "haproxy:3.2.26",
        options=(
            *l4_options,
            "--ulimit",
            "nofile=32768:32768",
            "--mount",
            f"type=bind,source={config},target=/usr/local/etc/haproxy/haproxy.cfg,readonly",
        ),
    )
    if stats_port is not None:
        docker("network", "connect", back, f"{prefix}-l4")
    configure_auth(work, f"http://{address('l4')}:8480", f"http://{address('select')}:8481")
    auth = work / "auth.yml"
    launch(
        "auth",
        f"victoriametrics/vmauth:{VM}",
        "-auth.config=/etc/vmauth.yml",
        options=(
            "--network",
            front,
            "-p",
            f"127.0.0.1:{port}:8427",
            "--mount",
            f"type=bind,source={auth},target=/etc/vmauth.yml,readonly",
        ),
    )
    docker("network", "connect", back, f"{prefix}-auth")
    print(f"已启动。客户端入口 http://127.0.0.1:{port}")
    if stats_port is not None:
        print(f"HAProxy 状态入口 http://127.0.0.1:{stats_port}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, help="New configuration directory (default: ~/<prefix>)")
    parser.add_argument("--prefix", default=PREFIX, help="Docker container and network name prefix")
    parser.add_argument("--port", type=int, default=PORT, help="Loopback client port")
    parser.add_argument("--stats-port", type=int, help="Optional loopback HAProxy status port")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_.-]*", args.prefix):
        parser.error("--prefix must be a valid Docker name prefix")
    if not 1 <= args.port <= 65535 or (args.stats_port is not None and not 1 <= args.stats_port <= 65535):
        parser.error("Ports must be between 1 and 65535")
    if args.port == args.stats_port:
        parser.error("--port and --stats-port must differ")
    os.umask(0o077)
    work = args.work if args.work is not None else Path.home() / args.prefix
    deploy(work.resolve(), prefix=args.prefix, port=args.port, stats_port=args.stats_port)


if __name__ == "__main__":
    main()
