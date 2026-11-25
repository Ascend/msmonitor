"""Check I8 against two running service replicas with shared PostgreSQL state."""

import argparse
import concurrent.futures
import http.client
import ipaddress
import json
import secrets
import ssl
import subprocess
import threading
import time
from pathlib import Path
from urllib.parse import urlsplit


REGISTER = "/api/v1/daemon/register"
HEARTBEAT = "/api/v1/daemon/heartbeat"
LIVE = "/livez"
READY = "/readyz"


def service_origin(value):
    try:
        origin = urlsplit(value)
        port = origin.port
    except ValueError as error:
        raise argparse.ArgumentTypeError("Invalid service origin") from error
    has_valid_authority = bool(origin.hostname) and origin.username is None and port != 0
    has_base_path = origin.path in ("", "/") and not (origin.query or origin.fragment)
    if origin.scheme not in {"http", "https"} or not has_valid_authority or not has_base_path:
        raise argparse.ArgumentTypeError("Use an origin such as https://center.example:8443")
    if origin.scheme == "http" and origin.hostname != "localhost":
        try:
            loopback = ipaddress.ip_address(origin.hostname).is_loopback
        except ValueError:
            loopback = False
        if not loopback:
            raise argparse.ArgumentTypeError("HTTP is supported for local loopback tests only")
    return origin


class Client:
    def __init__(self, origin, tls):
        self.origin = origin
        self.tls = tls

    def request(self, path, token=None, data=None):
        host, port = self.origin.hostname, self.origin.port
        if self.origin.scheme == "https":
            conn = http.client.HTTPSConnection(host, port, timeout=5, context=self.tls)
        else:
            conn = http.client.HTTPConnection(host, port, timeout=5)
        headers = {} if token is None else {"Authorization": "Bearer " + token}
        if data is not None:
            headers["Content-Type"] = "application/json"
        try:
            conn.request(
                "GET" if data is None else "POST",
                path,
                body=None if data is None else json.dumps(data),
                headers=headers,
            )
            response = conn.getresponse()
            return response.status, response.read()
        finally:
            conn.close()

    def expect(self, path, status=200, **request):
        actual, body = self.request(path, **request)
        if actual != status:
            raise AssertionError(f"{self.origin.netloc}{path}: expected HTTP {status}, received {actual}")
        return body


def registered_session(client, token, identity):
    result = json.loads(client.expect(REGISTER, token=token, data=identity))
    session = result.get("session_id")
    interval = result.get("heartbeat_interval_s")
    valid_interval = isinstance(interval, int) and not isinstance(interval, bool) and interval > 0
    if not isinstance(session, str) or not session or not valid_interval:
        raise AssertionError("Registration response lacks a session ID or heartbeat interval")
    return session


def heartbeat_payload(session, sequence, state="running"):
    return {"session_id": session, "status_seq": sequence, "collect_state": state}


def check_heartbeat(client, token, session, sequence, state="running"):
    response = client.expect(HEARTBEAT, token=token, data=heartbeat_payload(session, sequence, state))
    if json.loads(response).get("accepted") is not True:
        raise AssertionError("Heartbeat response was not accepted")


def check_sessions(replica_a, replica_b, token_a, token_b, daemon_id):
    for client in (replica_a, replica_b):
        client.expect(LIVE)
        client.expect(READY)
    identity = {"daemon_id": daemon_id, "instance_id": "boot-1"}
    replica_a.expect(REGISTER, status=401, data=identity)

    start = threading.Barrier(2, timeout=5)

    def register_concurrently(client):
        start.wait()
        return registered_session(client, token_a, identity)

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        a = executor.submit(register_concurrently, replica_a)
        b = executor.submit(register_concurrently, replica_b)
        session = a.result()
        if b.result() != session:
            raise AssertionError("Concurrent duplicate registrations returned different sessions")
    if registered_session(replica_b, token_a, identity) != session:
        raise AssertionError("Cross-replica registration retry returned a different session")

    other_session = registered_session(replica_b, token_b, identity)
    if other_session == session:
        raise AssertionError("Different scopes share the same registration session")
    replica_b.expect(HEARTBEAT, status=403, token=token_b, data=heartbeat_payload(session, 1))
    replica_a.expect(HEARTBEAT, status=403, token=token_a, data=heartbeat_payload(other_session, 1))
    replica_a.expect(HEARTBEAT, status=400, token=token_a, data=heartbeat_payload(session, -1))
    check_heartbeat(replica_b, token_a, session, 1)
    check_heartbeat(replica_a, token_b, other_session, 1)

    next_identity = {**identity, "instance_id": "boot-2"}
    next_session = registered_session(replica_b, token_a, next_identity)
    if next_session == session:
        raise AssertionError("Replacement instance reused the previous session")
    replica_a.expect(HEARTBEAT, status=409, token=token_a, data=heartbeat_payload(session, 2))
    replica_a.expect(REGISTER, status=409, token=token_a, data=identity)
    check_heartbeat(replica_a, token_a, next_session, 2)
    check_heartbeat(replica_b, token_a, next_session, 1, "stale")
    check_heartbeat(replica_b, token_a, next_session, 2, "duplicate")
    print("PASS: health, concurrent registration, scope isolation, replacement and heartbeat retries", flush=True)
    return next_session, next_identity


def check_metadata(replica_a, replica_b, token_a, token_b):
    identity = {"daemon_id": "i8-meta-" + secrets.token_hex(8), "instance_id": "boot-1"}
    registration = {
        "job_id": "job-a",
        "run_id": "run-a",
        "node_id": "node-a",
        "daemon_version": "v1.0",
        "rank_capabilities": ["rank-0", "rank-1"],
    }
    for metadata in (None, {"scope": "scope-b"}, {"rank_capabilities": ["rank-0", "rank-0"]}):
        replica_a.expect(REGISTER, status=400, token=token_a, data={**identity, "metadata": metadata})
    oversized = {"rank_capabilities": [f"{i:02d}-" + "a" * 125 for i in range(64)]}
    replica_a.expect(REGISTER, status=413, token=token_a, data={**identity, "metadata": oversized})

    response = json.loads(replica_a.expect(REGISTER, token=token_a, data={**identity, "metadata": registration}))
    session = response["session_id"]
    retry = json.loads(
        replica_b.expect(
            REGISTER,
            token=token_a,
            data={
                **identity,
                "metadata": {**registration, "job_id": "must-not-replace"},
            },
        )
    )
    if response.keys() != {"session_id", "heartbeat_interval_s"} or retry != response:
        raise AssertionError("Registration metadata changed the legacy response or retry semantics")

    rejected_values = ({"pending_files": True}, {"cpu_percent": 101}, {"rss_bytes": 2**63})
    for metadata in rejected_values:
        replica_a.expect(
            HEARTBEAT, status=400, token=token_a, data={**heartbeat_payload(session, 100), "metadata": metadata}
        )
    telemetry = {
        "app_version": "v1.0",
        "pending_files": 3,
        "dropped_records": 0,
        "cpu_percent": 12.5,
        "rss_bytes": 8192,
    }
    for client, seq, metadata in (
        (replica_b, 1, telemetry),
        (replica_a, 2, {"pending_files": 4}),
        (replica_a, 1, {"pending_files": 90}),
        (replica_b, 2, {"pending_files": 99}),
    ):
        body = client.expect(HEARTBEAT, token=token_a, data={**heartbeat_payload(session, seq), "metadata": metadata})
        if json.loads(body) != {"accepted": True}:
            raise AssertionError("Heartbeat metadata changed the legacy response")
    replica_b.expect(
        HEARTBEAT,
        status=403,
        token=token_b,
        data={**heartbeat_payload(session, 100), "metadata": {"pending_files": 100}},
    )
    check_heartbeat(replica_b, token_a, session, 3)
    print(f"metadata_daemon_id={identity['daemon_id']}", flush=True)
    print("PASS: metadata request limits, response compatibility, retry and scope isolation", flush=True)


def check_commands(a, b, tokens, operator_prefix=None):
    scope, token = next(iter(tokens.items()))
    other = list(tokens.values())[1]
    daemon = "command-st-" + secrets.token_hex(8)
    identity = {"daemon_id": daemon, "instance_id": "boot1"}
    session = registered_session(a, token, identity)
    lab = Path(__file__).resolve().parents[3] / "deploy/center/control/lab.sh"

    def operator(action, *extra, request_id="export-1"):
        return json.loads(
            subprocess.check_output(
                [
                    *(
                        operator_prefix
                        or ["bash", str(lab), "compose", "exec", "-T", "app1", "python", "-m", "registry"]
                    ),
                    action,
                    "--scope",
                    scope,
                    "--daemon-id",
                    daemon,
                    "--request-id",
                    request_id,
                    *extra,
                ]
            )
        )

    poll = "/api/v1/daemon/poll-commands"
    ack = "/api/v1/daemon/ack"
    payload = {"session_id": session, "wait_s": 1}
    assert json.loads(a.expect(poll, token=token, data=payload)) == {"command": None}
    params = ("--kind", "export", "--rank", "0", "--start-ns", "10", "--end-ns", "20")
    assert operator("enqueue", *params) == operator("enqueue", *params)
    b.expect(poll, status=403, token=other, data=payload)
    received = json.loads(b.expect(poll, token=token, data=payload))
    assert received["command"] == {
        "request_id": "export-1",
        "version": 1,
        "kind": "export",
        "rank": 0,
        "start_ns": 10,
        "end_ns": 20,
    }
    assert json.loads(a.expect(poll, token=token, data=payload)) == received
    new = registered_session(b, token, {**identity, "instance_id": "boot2"})
    result = {"rank": 0, "status": "success", "uri": "evidence/manifest.json", "sha256": "a" * 64}
    final = {"session_id": session, "request_id": "export-1", "version": 1, "result": result}
    b.expect(ack, status=409, token=token, data=final)
    final["session_id"] = new
    b.expect(ack, status=409, token=token, data=final)
    assert json.loads(a.expect(poll, token=token, data={"session_id": new, "wait_s": 0})) == received
    for replica in (a, b):
        replica.expect(ack, token=token, data=final)
    a.expect(ack, status=409, token=token, data={**final, "result": {**result, "status": "failed"}})
    assert operator("show")["result"] == result
    assert json.loads(b.expect(poll, token=token, data={"session_id": new, "wait_s": 0})) == {"command": None}
    for kind, extra in (("collect", ["--duration-s", "30"]), ("stop", [])):
        operator("enqueue", "--kind", kind, "--rank", "2", *extra, request_id=kind)
        data = {"session_id": new, "wait_s": 0}
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            reads = [pool.submit(replica.expect, poll, token=token, data=data) for replica in (a, b)]
            assert [json.loads(read.result())["command"]["request_id"] for read in reads] == [kind, kind]
        final = {
            "session_id": new,
            "request_id": kind,
            "version": 1,
            "result": {"rank": 2, "status": "partial", "missing_ranges_ns": [[10, 20]]},
        }
        invalid = {**final["result"], "missing_ranges_ns": [[20, 10]]}
        a.expect(ack, status=400, token=token, data={**final, "result": invalid})
        if kind == "collect":
            operator("enqueue", "--kind", "stop", "--rank", "2", request_id="interrupt")
            stop = json.loads(b.expect(poll, token=token, data=data))["command"]
            assert stop["request_id"] == "interrupt" and stop["kind"] == "stop"
            a.expect(ack, token=token, data={**final, "request_id": "interrupt"})
            assert json.loads(a.expect(poll, token=token, data=data))["command"]["request_id"] == kind
        b.expect(ack, token=token, data=final)
        assert operator("show", request_id=kind)["result"] == final["result"]
    print("PASS: collect, stop, export, command redelivery and durable per-rank results")


def check_summary(a, b, token, other):
    path = "/api/v1/daemon/submit-summary"
    daemon = "summary-" + secrets.token_hex(8)
    identity = {"daemon_id": daemon, "instance_id": "first"}
    session = registered_session(a, token, identity)
    rows = [{"metric": "duration", "p99": 123, "rank": 0}]
    payload = {"batch_id": "batch-1", "session_id": session, "schema_version": 1, "records": rows}

    def submit(client, data):
        result = json.loads(client.expect(path, token=token, data=data))
        assert result == {"persisted": True, "batch_id": "batch-1", "count": 1}

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(submit, client, payload) for client in (a, b)]
        for future in futures:
            future.result()
    b.expect(path, status=403, token=other, data=payload)
    for changes in ({"schema_version": True}, {"records": []}, {"records": [1]}, {"schema_version": 2}):
        b.expect(path, status=400, token=token, data={**payload, **changes})
    b.expect(path, status=413, token=token, data={**payload, "records": [{"text": "x" * 262144}]})
    b.expect(path, status=409, token=token, data={**payload, "records": [{"rank": 9}]})
    new = registered_session(b, token, {**identity, "instance_id": "second"})
    a.expect(path, status=409, token=token, data=payload)
    submit(a, {**payload, "session_id": new})
    print("PASS: summary persistence acknowledgement, concurrent retry, conflict and session isolation")


def wait_until(probe, seconds, description):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if probe():
            return
        time.sleep(0.5)
    raise AssertionError(f"Timed out waiting for {description}")


def check_replica_failure(replica_a, replica_b, token, session, identity, timeout):
    def replica_a_down():
        try:
            replica_a.request(LIVE)
        except ssl.SSLError:
            raise
        except OSError:
            return True
        return False

    print(f"Stop replica A ({replica_a.origin.netloc}) now. Keep B and PostgreSQL running.", flush=True)
    wait_until(replica_a_down, timeout, "replica A to stop responding")
    replica_b.expect(READY)
    if registered_session(replica_b, token, identity) != session:
        raise AssertionError("Replica B lost the existing session")
    check_heartbeat(replica_b, token, session, 3)
    print("PASS: replica B continued the existing session while replica A was down", flush=True)


def check_db_recovery(client, token, session, identity, timeout):
    print("Stop the test PostgreSQL instance now. Keep replica B running.", flush=True)
    wait_until(lambda: client.request(READY)[0] == 503, timeout, "readiness HTTP 503")
    client.expect(LIVE)
    payload = heartbeat_payload(session, 4)
    client.expect(HEARTBEAT, status=503, token=token, data=payload)
    summary = {"session_id": session, "batch_id": "db-recovery", "schema_version": 1, "records": [{"ok": 1}]}
    client.expect("/api/v1/daemon/submit-summary", status=503, token=token, data=summary)
    client.expect("/api/v1/daemon/poll-commands", status=503, token=token, data={"session_id": session, "wait_s": 0})
    print("PASS: database outage leaves liveness up and rejects state writes. Start PostgreSQL now.", flush=True)
    wait_until(lambda: client.request(READY)[0] == 200, timeout, "database recovery")
    if registered_session(client, token, identity) != session:
        raise AssertionError("Session changed after PostgreSQL recovery")
    check_heartbeat(client, token, session, 4)
    client.expect("/api/v1/daemon/submit-summary", token=token, data=summary)
    print("PASS: readiness and the original session recovered after the database returned", flush=True)


def validate_replica_arguments(parser, args):
    if args.origin_a.netloc == args.origin_b.netloc:
        parser.error("Supply two different replica origins")
    if args.transition_timeout <= 0:
        parser.error("--transition-timeout must be positive")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-a", type=service_origin, required=True)
    parser.add_argument("--origin-b", type=service_origin, required=True)
    parser.add_argument("--tokens", type=Path, required=True, help="JSON scope-to-token map with two scopes")
    parser.add_argument("--ca", type=Path, help="CA certificate for the HTTPS origins")
    parser.add_argument("--verify-metadata", action="store_true", help="Check optional register and heartbeat metadata")
    parser.add_argument("--wait-for-failover", action="store_true", help="Wait for you to stop replica A")
    parser.add_argument(
        "--wait-for-db-recovery", action="store_true", help="Wait for you to stop and start the test DB"
    )
    parser.add_argument("--transition-timeout", type=int, default=90, help="Seconds to wait for each manual action")
    parser.add_argument("--verify-commands", action="store_true", help="Exercise the Compose lab command queue")
    parser.add_argument("--verify-summary", action="store_true")
    args = parser.parse_args()
    validate_replica_arguments(parser, args)
    tokens = json.loads(args.tokens.read_text(encoding="utf-8"))
    if not isinstance(tokens, dict) or len(tokens) < 2:
        parser.error("The token file must contain at least two scopes")
    token_a, token_b = list(tokens.values())[:2]
    if (
        not all(
            isinstance(token, str) and token.isascii() and token.isprintable() and token for token in (token_a, token_b)
        )
        or token_a == token_b
    ):
        parser.error("The first two scopes must have different valid tokens")
    tls = ssl.create_default_context(cafile=str(args.ca) if args.ca else None)
    a, b = Client(args.origin_a, tls), Client(args.origin_b, tls)
    daemon_id = "i8-st-" + secrets.token_hex(8)
    print(f"daemon_id={daemon_id}", flush=True)
    session, identity = check_sessions(a, b, token_a, token_b, daemon_id)
    if args.verify_summary:
        check_summary(a, b, token_a, token_b)
    if args.verify_commands:
        check_commands(a, b, tokens)
    if args.verify_metadata:
        check_metadata(a, b, token_a, token_b)
    if args.wait_for_failover:
        check_replica_failure(a, b, token_a, session, identity, args.transition_timeout)
    if args.wait_for_db_recovery:
        check_db_recovery(b, token_a, session, identity, args.transition_timeout)


if __name__ == "__main__":
    main()
