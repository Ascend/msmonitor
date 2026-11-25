"""Check the I5 HAProxy status port or run an isolated Docker backend failure fixture."""

import argparse
from contextlib import ExitStack
from dataclasses import dataclass
from http.client import HTTPConnection, HTTPException
from pathlib import Path
import re
import subprocess
import tempfile
from textwrap import dedent
import time
from urllib.parse import SplitResult, urlsplit
from uuid import uuid4


_DEFAULT_ORIGIN = "http://127.0.0.1:18404"
_METRIC = re.compile(r'^haproxy_backend_active_servers\{proxy="writers"\}\s+([0-9]+)$', re.MULTILINE)
_MAX_METRICS_BYTES = 2 * 1024 * 1024
_GATEWAY = Path(__file__).resolve().parents[3] / "deploy" / "center" / "metrics" / "gateway.yaml"

_FIXTURE = """\
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

class Writer(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/-/healthy":
            self.send_response(503 if Path("/tmp/i5-unhealthy").exists() else 200)
        elif self.path == "/fixture":
            self.send_response(200)
        else:
            self.send_response(404)
        self.end_headers()

ThreadingHTTPServer(("0.0.0.0", 8480), Writer).serve_forever()
"""


@dataclass(frozen=True)
class Status:
    live: int
    ready: int
    active_writers: int


def parse_origin(value: str) -> SplitResult:
    origin = urlsplit(value)
    if origin.scheme != "http" or not origin.hostname or origin.port is None:
        raise ValueError("origin must be an HTTP origin with an explicit port")
    if origin.path not in ("", "/") or origin.username is not None or origin.query or origin.fragment:
        raise ValueError("origin must contain only an HTTP scheme, host and port")
    return origin


def get(origin: SplitResult, path: str, *, read_metrics: bool = False) -> tuple[int, bytes]:
    connection = HTTPConnection(origin.hostname, origin.port, timeout=3)
    try:
        connection.request("GET", path)
        with connection.getresponse() as response:
            body = response.read(_MAX_METRICS_BYTES + 1) if read_metrics else b""
            return response.status, body
    finally:
        connection.close()


def inspect_gateway(origin: SplitResult) -> Status:
    live, _ = get(origin, "/livez")
    ready, _ = get(origin, "/readyz")
    status, body = get(origin, "/metrics", read_metrics=True)
    if status != 200 or len(body) > _MAX_METRICS_BYTES:
        raise RuntimeError(f"invalid HAProxy metrics response (HTTP {status}, {len(body)} bytes)")
    metric = _METRIC.search(body.decode("utf-8"))
    if not metric:
        raise RuntimeError('missing haproxy_backend_active_servers{proxy="writers"}')
    return Status(live, ready, int(metric.group(1)))


def wait_for(check, expected) -> None:
    deadline = time.monotonic() + 20
    while True:
        try:
            actual = check()
        except (OSError, HTTPException, RuntimeError) as error:
            actual = error
        if actual == expected:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError(f"expected {expected}, last observation was {actual}")
        time.sleep(0.25)


def docker(*args: str, check: bool = True) -> str:
    result = subprocess.run(["docker", *args], check=check, capture_output=True, text=True, timeout=120)
    if result.returncode:
        print(result.stderr.strip())
    return result.stdout.strip()


def published_origin(container: str, port: int) -> SplitResult:
    target = docker("port", container, f"{port}/tcp")
    if not re.fullmatch(r"127\.0\.0\.1:[1-9][0-9]*", target):
        raise RuntimeError(f"expected a loopback-only test port for {container}, got {target!r}")
    return parse_origin(f"http://{target}")


def gateway_config() -> str:
    document = _GATEWAY.read_text(encoding="utf-8").split("\n---\n", 1)[0]
    _, separator, value = document.partition("  haproxy.cfg: |\n")
    if not separator or not value:
        raise RuntimeError("HAProxy config literal is missing from the gateway ConfigMap")
    return dedent(value) + "\n"


def docker_fixture() -> None:
    writer_image = "python:3.12-slim-bookworm"
    gateway_image = "haproxy:3.2.26"
    for image in (writer_image, gateway_image):
        if subprocess.run(
            ["docker", "image", "inspect", image], check=False, capture_output=True, timeout=30
        ).returncode:
            # Keep image download time and registry errors separate from fixture startup.
            subprocess.run(["docker", "pull", image], check=True, timeout=600)
    prefix = f"msmonitor-i5-{uuid4().hex[:12]}"
    writer = f"{prefix}-writer"
    gateway = f"{prefix}-gateway"
    with tempfile.TemporaryDirectory(prefix="msmonitor-i5-") as directory, ExitStack() as cleanup:
        config = Path(directory) / "haproxy.cfg"
        config.write_text(gateway_config(), encoding="utf-8")
        docker("network", "create", prefix)
        cleanup.callback(docker, "network", "rm", prefix, check=False)

        docker(
            "run",
            "-d",
            "--name",
            writer,
            "--network",
            prefix,
            "--network-alias",
            "vminsert-msmonitor-headless.i5fixture.svc.cluster.local",
            "-p",
            "127.0.0.1::8480",
            writer_image,
            "python",
            "-u",
            "-c",
            _FIXTURE,
        )
        cleanup.callback(docker, "rm", "-f", writer, check=False)
        writer_origin = published_origin(writer, 8480)
        wait_for(lambda: get(writer_origin, "/-/healthy")[0], 200)

        docker(
            "run",
            "-d",
            "--name",
            gateway,
            "--network",
            prefix,
            "-p",
            "127.0.0.1::8404",
            "-e",
            "POD_NAMESPACE=i5fixture",
            "-e",
            "CLUSTER_DOMAIN=cluster.local",
            "-v",
            f"{config}:/usr/local/etc/haproxy/haproxy.cfg:ro",
            gateway_image,
        )
        cleanup.callback(docker, "rm", "-f", gateway, check=False)
        origin = published_origin(gateway, 8404)
        try:
            wait_for(lambda: inspect_gateway(origin), Status(200, 200, 1))
            print("PASS I5 initial readiness and one active writer")
            docker("exec", writer, "python", "-c", "from pathlib import Path; Path('/tmp/i5-unhealthy').touch()")
            wait_for(lambda: get(writer_origin, "/-/healthy")[0], 503)
            wait_for(lambda: inspect_gateway(origin), Status(200, 503, 0))
            wait_for(lambda: get(writer_origin, "/fixture")[0], 200)
            print("PASS I5 HTTP-unhealthy writer removed while its TCP port still serves requests")
            docker("exec", writer, "python", "-c", "from pathlib import Path; Path('/tmp/i5-unhealthy').unlink()")
            wait_for(lambda: inspect_gateway(origin), Status(200, 200, 1))
            print("PASS I5 writer recovery and readiness restored")
        except (OSError, HTTPException, RuntimeError):
            print(docker("logs", "--tail", "30", gateway, check=False))
            raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--origin", default=_DEFAULT_ORIGIN, help="HAProxy status HTTP origin with port")
    mode.add_argument("--docker-fixture", action="store_true", help="run isolated Docker HTTP health failure fixture")
    args = parser.parse_args()
    try:
        if args.docker_fixture:
            docker_fixture()
            return
        status = inspect_gateway(parse_origin(args.origin))
        if status.live != 200 or status.ready != 200 or status.active_writers < 1:
            raise RuntimeError(f"gateway is not ready: {status}")
        print(f"PASS I5 liveness, readiness and metrics ({status.active_writers} active writers)")
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr.strip())
        parser.exit(1, f"I5 check failed: {error}\n")
    except (OSError, HTTPException, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"I5 check failed: {error}\n")


if __name__ == "__main__":
    main()
