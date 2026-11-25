"""Run the center image in standalone mode or hand off to the I8 command in cluster mode."""

import fcntl
import json
import os
import signal
import subprocess  # nosec B404  # Only argv-based calls to bundled executables.
import sys
import tempfile
import time
from pathlib import Path
from string import Template
from urllib.error import URLError
from urllib.request import ProxyHandler, build_opener

import visualization
from center_config import auth_config, load_config, retention_period, registration_urls

APP = Path(__file__).resolve().parent
CONFIG = Path("/etc/msmonitor")
DATA = Path("/data")
PG = Path("/usr/lib/postgresql/15/bin")
ADDRESSES = {
    "gateway_health": "127.0.0.1:8404",
    "control": "127.0.0.1:8080",
    "auth_health": "127.0.0.1:8428",
    "insert": "127.0.0.1:8480",
    "select": "127.0.0.1:8481",
    "storage": "127.0.0.1:8482",
    "storage_write": "127.0.0.1:8400",
    "storage_read": "127.0.0.1:8401",
    "auth": "127.0.0.1:8427",
}
PUBLIC_PORT = 8443
CHECK_URLS = {
    "gateway_health": f"http://{ADDRESSES['gateway_health']}/readyz",
    "control": f"http://{ADDRESSES['control']}/readyz",
    "auth_health": f"http://{ADDRESSES['auth_health']}/health",
    "insert": f"http://{ADDRESSES['insert']}/-/healthy",
    "select": f"http://{ADDRESSES['select']}/-/healthy",
    "storage": f"http://{ADDRESSES['storage']}/-/healthy",
}
LOCAL_HTTP = build_opener(ProxyHandler({}))


def healthy(url):
    try:
        with LOCAL_HTTP.open(url, timeout=2) as response:
            return response.status == 200
    except (URLError, OSError):
        return False


class Processes:
    """Fail the container when a required child exits and stop children in reverse dependency order."""

    def __init__(self):
        self.children = []
        self.stopping = False

    def request_stop(self, *_):
        self.stopping = True

    def start(self, command, env=None, stop_signal=signal.SIGTERM):
        if self.stopping:
            raise InterruptedError
        # pylint: disable-next=consider-using-with
        child = subprocess.Popen(command, env=env)  # nosec B603  # Commands use bundled binaries and validated fields.
        self.children.append((Path(command[0]).name, child, stop_signal))
        return child

    def check(self):
        if self.stopping:
            raise InterruptedError
        for name, child, _ in self.children:
            if child.poll() is not None:
                raise RuntimeError(f"{name} exited with status {child.returncode}")

    def wait_for(self, condition, description):
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            self.check()
            if condition():
                return
            time.sleep(0.25)
        raise RuntimeError(f"Timed out waiting for {description}")

    def close(self):
        deadline = time.monotonic() + 35
        for _, child, stop_signal in reversed(self.children):
            if child.poll() is None:
                child.send_signal(stop_signal)
                try:
                    timeout = min(20, max(0.1, deadline - time.monotonic()))
                    child.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


def run_once(command, env=None):
    subprocess.run(command, env=env, check=True, timeout=90)  # nosec B603  # Bundled binaries only.


def prepare_postgres(data):
    if data.exists():
        if not (data / "PG_VERSION").is_file():
            raise ValueError("Existing PostgreSQL data directory has no PG_VERSION")
        return
    with tempfile.TemporaryDirectory(prefix=".pg-init-", dir=DATA) as stage:
        initial_data = Path(stage) / "db"
        run_once(
            [
                str(PG / "initdb"),
                "-D",
                str(initial_data),
                "--username=msmonitor",
                "--encoding=UTF8",
                "--locale=C",
                "--auth-local=peer",
                "--auth-host=scram-sha-256",
            ]
        )
        initial_data.rename(data)


def start_postgres(processes, work):
    data = DATA / "postgres"
    prepare_postgres(data)
    socket = work / "postgres"
    socket.mkdir(mode=0o700)
    processes.start(
        [
            str(PG / "postgres"),
            "-D",
            str(data),
            "-c",
            "listen_addresses=",
            "-c",
            f"unix_socket_directories={socket}",
        ],
        stop_signal=signal.SIGINT,
    )

    def ready():
        result = subprocess.run(  # nosec B603  # Fixed pg_isready executable and local socket.
            [str(PG / "pg_isready"), "-h", str(socket), "-t", "1"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=3,
        )
        return result.returncode == 0

    processes.wait_for(ready, "PostgreSQL")
    env = os.environ.copy()
    env.pop("I8_DATABASE_URL_FILE", None)
    env.update(I8_DATABASE_URL=f"dbname=postgres user=msmonitor host={socket}", UVICORN_HOST="127.0.0.1")
    return env


def start_metrics(processes, auth_file, retention):
    processes.start(
        [
            "/usr/local/bin/vmstorage",
            f"-httpListenAddr={ADDRESSES['storage']}",
            f"-vminsertAddr={ADDRESSES['storage_write']}",
            f"-vmselectAddr={ADDRESSES['storage_read']}",
            f"-storageDataPath={DATA / 'victoriametrics'}",
            f"-retentionPeriod={retention}",
            "-dedup.minScrapeInterval=1ms",
        ]
    )
    processes.wait_for(lambda: healthy(CHECK_URLS["storage"]), "vmstorage")
    processes.start(
        [
            "/usr/local/bin/vminsert",
            f"-httpListenAddr={ADDRESSES['insert']}",
            f"-storageNode={ADDRESSES['storage_write']}",
        ]
    )
    processes.start(
        [
            "/usr/local/bin/vmselect",
            f"-httpListenAddr={ADDRESSES['select']}",
            f"-storageNode={ADDRESSES['storage_read']}",
            "-dedup.minScrapeInterval=1ms",
        ]
    )
    processes.start(
        [
            "/usr/local/bin/vmauth",
            f"-auth.config={auth_file}",
            f"-httpListenAddr={ADDRESSES['auth']}",
            f"-httpInternalListenAddr={ADDRESSES['auth_health']}",
        ]
    )


def visualization_enabled():
    return os.getenv("MSMONITOR_VISUALIZATION") == "1"


def standalone(command):
    config = load_config(CONFIG / "center.json")
    retention = retention_period(config)

    processes = Processes()
    signal.signal(signal.SIGTERM, processes.request_stop)
    signal.signal(signal.SIGINT, processes.request_stop)
    with (DATA / ".center.lock").open("a", encoding="utf-8") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with tempfile.TemporaryDirectory(prefix="msmonitor-") as directory:
            work = Path(directory)
            proxy = work / "haproxy.cfg"
            template = Template((APP / "standalone-haproxy.cfg").read_text(encoding="utf-8"))
            proxy_config = template.substitute(ADDRESSES, public_port=PUBLIC_PORT)
            if visualization_enabled():
                proxy_config += visualization.query_proxy(config, ADDRESSES["auth"])
            proxy.write_text(proxy_config, encoding="utf-8")
            run_once(["/usr/sbin/haproxy", "-c", "-f", str(proxy)])
            auth_file = work / "auth.yaml"
            auth_file.write_text(
                auth_config(config["tenants"], f"http://{ADDRESSES['insert']}", f"http://{ADDRESSES['select']}"),
                encoding="utf-8",
            )
            tokens = work / "tokens.json"
            tokens.write_text(json.dumps(config["i8_tokens"]), encoding="utf-8")

            try:
                env = start_postgres(processes, work)
                env["I8_TOKENS_FILE"] = str(tokens)
                env["I8_WRITE_URLS"] = json.dumps(registration_urls(config))
                run_once([sys.executable, "-m", "registry", "migrate"], env)
                processes.start(command, env)
                start_metrics(processes, auth_file, retention)
                processes.wait_for(
                    lambda: all(healthy(url) for name, url in CHECK_URLS.items() if name != "gateway_health"),
                    "internal services",
                )
                processes.start(["/usr/sbin/haproxy", "-db", "-f", str(proxy)])
                if visualization_enabled():
                    visualization.start(processes)
                while True:
                    processes.check()
                    time.sleep(0.25)
            except InterruptedError:
                return
            finally:
                processes.close()


def main():
    mode = os.getenv("MSMONITOR_MODE", "standalone")
    if sys.argv[1:] == ["healthcheck"]:
        urls = (CHECK_URLS["control"],) if mode == "cluster" else CHECK_URLS.values()
        if mode == "standalone" and visualization_enabled():
            urls = (*urls, *visualization.HEALTH_URLS)
        return 0 if all(map(healthy, urls)) else 1
    if mode not in ("standalone", "cluster") or len(sys.argv) < 2:
        raise ValueError("Expected standalone or cluster mode and an I8 command")
    if mode == "cluster":
        os.execvp(sys.argv[1], sys.argv[1:])  # nosec B606  # Docker CMD, controlled by the deployer.

    os.umask(0o077)
    standalone(sys.argv[1:])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except json.JSONDecodeError:
        sys.exit("Invalid JSON in /etc/msmonitor/center.json")
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(f"Center startup/runtime failure: {error}")
