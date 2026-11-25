"""Offer scheduled heartbeat load from synthetic I8 daemons to two replicas."""

import argparse
import asyncio
import concurrent.futures
import http.client
import json
import math
import secrets
import ssl
import threading
import time
from collections import Counter
from pathlib import Path

from test_control_service import HEARTBEAT, REGISTER, heartbeat_payload, service_origin


class LoadClient:
    def __init__(self, origins, token, ca):
        self.origins = origins
        self.headers = {"Authorization": "Bearer " + token, "Content-Type": "application/json"}
        self.tls = ssl.create_default_context(cafile=str(ca) if ca else None)
        self.local = threading.local()
        self.connections = []
        self.lock = threading.Lock()

    def connection(self, replica):
        if not hasattr(self.local, "connections"):
            self.local.connections = {}
            self.local.last_response = {}
        if replica not in self.local.connections:
            origin = self.origins[replica]
            if origin.scheme == "https":
                conn = http.client.HTTPSConnection(origin.hostname, origin.port, timeout=5, context=self.tls)
            else:
                conn = http.client.HTTPConnection(origin.hostname, origin.port, timeout=5)
            self.local.connections[replica] = conn
            with self.lock:
                self.connections.append(conn)
        conn = self.local.connections[replica]
        if time.monotonic() - self.local.last_response.get(replica, 0) > 2:
            conn.close()
        return conn

    def post(self, replica, path, payload):
        conn = self.connection(replica)
        try:
            conn.request("POST", path, body=json.dumps(payload), headers=self.headers)
            response = conn.getresponse()
            body = response.read()
            if response.status != 200:
                return f"http_{response.status}", None
            data = json.loads(body)
            return ("ok", data) if isinstance(data, dict) else ("invalid_response", None)
        except (OSError, http.client.HTTPException, ValueError) as error:
            conn.close()
            return type(error).__name__, None
        finally:
            self.local.last_response[replica] = time.monotonic()

    def close(self):
        for conn in self.connections:
            conn.close()


def percentile(values, fraction):
    return round(sorted(values)[math.ceil(len(values) * fraction) - 1], 2) if values else None


def run_phase(executor, operation, count, rate, args, *, steady=False):
    pending = set()
    outcomes = Counter()
    latencies = []
    lags = []
    late = 0
    queue_waits = 0
    start = time.monotonic()

    def execute(index, due):
        sent = time.monotonic()
        outcome = operation(index)
        return outcome, (sent - due) * 1000, (time.monotonic() - sent) * 1000

    def collect(finished):
        nonlocal late
        for future in finished:
            outcome, lag, latency = future.result()
            outcomes[outcome] += 1
            latencies.append(latency)
            lags.append(lag)
            late += lag > args.max_lag_ms

    for index in range(count):
        due = start + index / rate
        time.sleep(max(0, due - time.monotonic()))
        if pending:
            finished, pending = concurrent.futures.wait(pending, timeout=0)
            collect(finished)
        if len(pending) >= args.workers * 2:
            queue_waits += 1
            # A short backlog can drain within the existing start-lag budget.
            # Keep the original due time when the worker eventually starts.
            remaining = due + args.max_lag_ms / 1000 - time.monotonic()
            finished, pending = concurrent.futures.wait(
                pending, timeout=max(0, remaining), return_when=concurrent.futures.FIRST_COMPLETED
            )
            if not finished:
                outcomes["client_queue_full"] += 1
                continue
            collect(finished)
        pending.add(executor.submit(execute, index, due))

    collect(concurrent.futures.as_completed(pending))
    elapsed = max(time.monotonic() - start, count / rate)
    p99 = percentile(latencies, 0.99)
    successful = outcomes.pop("ok", 0)
    achieved_rate = successful / elapsed
    passed = (
        successful == count
        and not late
        and p99 is not None
        and p99 <= args.max_p99_ms
        and (not steady or achieved_rate >= rate * 0.99)
    )
    return {
        "passed": passed,
        "offered_rps": round(rate, 2),
        "successful_rps": round(achieved_rate, 2),
        "duration_s": round(elapsed, 3),
        "expected": count,
        "successful": successful,
        "failures": dict(outcomes),
        "late_starts": late,
        "client_queue_waits": queue_waits,
        "start_lag_p99_ms": percentile(lags, 0.99),
        "latency_p99_ms": p99,
    }


async def poll_load(args, token, sessions, stop):
    """Keep one logical polling loop per registered daemon during the heartbeat phase."""
    counts = Counter()
    errors = Counter()
    tls = ssl.create_default_context(cafile=str(args.ca) if args.ca else None)

    async def request(origin, session):
        encrypted = origin.scheme == "https"
        reader, writer = await asyncio.open_connection(
            origin.hostname, origin.port or (443 if encrypted else 80), ssl=tls if encrypted else None
        )
        try:
            body = json.dumps({"session_id": session, "wait_s": 10}).encode()
            headers = (
                f"POST /api/v1/daemon/poll-commands HTTP/1.1\r\nHost: {origin.netloc}\r\n"
                f"Authorization: Bearer {token}\r\nContent-Type: application/json\r\n"
                f"Content-Length: {len(body)}\r\nConnection: close\r\n\r\n"
            )
            writer.write(headers.encode("ascii") + body)
            await writer.drain()
            status = (await reader.readline()).split()
            if len(status) < 2 or status[1] != b"200":
                raise ValueError("http_" + (status[1].decode("ascii") if len(status) > 1 else "eof"))
            while True:
                line = await reader.readline()
                if line == b"\r\n":
                    break
                if not line:
                    raise ValueError("incomplete_headers")
            data = json.loads(await reader.read())
            if data["command"] is not None:
                raise ValueError("unexpected_command_for_synthetic_node")
            return data.get("retry_after_s", 0)
        finally:
            writer.close()
            await writer.wait_closed()

    async def poll(index, session):
        await asyncio.sleep(index * args.interval / len(sessions))
        first = True
        while not stop.is_set():
            try:
                delay = await asyncio.wait_for(request((args.origin_a, args.origin_b)[index % 2], session), 20)
            except (OSError, ValueError, asyncio.TimeoutError, KeyError) as error:
                errors[str(error)[:80]] += 1
                return
            counts["requests_ok"] += 1
            counts["clients_ok"] += int(first)
            first = False
            if delay:
                counts["polls_deferred"] += 1
                await asyncio.sleep(delay)

    await asyncio.gather(*(poll(i, s) for i, s in enumerate(sessions)))
    return {**counts, "errors": dict(errors), "passed": not errors and counts["clients_ok"] == len(sessions)}


def run(args, token):
    client = LoadClient((args.origin_a, args.origin_b), token, args.ca)
    prefix = "i8-load-" + secrets.token_hex(6)
    sessions = [None] * args.daemons

    def register(index):
        payload = {"daemon_id": f"{prefix}-{index}", "instance_id": "boot-1"}
        status, result = client.post(index % 2, REGISTER, payload)
        if status == "ok":
            session = result.get("session_id")
            if not isinstance(session, str) or not session:
                return "invalid_session"
            sessions[index] = session
        return status

    def heartbeat(index):
        cycle, daemon = divmod(index, args.daemons)
        payload = heartbeat_payload(sessions[daemon], cycle + 1)
        status, result = client.post((daemon + cycle + 1) % 2, HEARTBEAT, payload)
        return "not_accepted" if status == "ok" and result.get("accepted") is not True else status

    print(f"run={prefix} daemons={args.daemons} interval_s={args.interval} cycles={args.cycles}", flush=True)
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
            registration = run_phase(executor, register, args.daemons, args.registration_rps, args)
            print(json.dumps({"registration": registration}), flush=True)
            if not registration["passed"]:
                return False
            stop = threading.Event()
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as background:
                polls = (
                    background.submit(asyncio.run, poll_load(args, token, sessions, stop)) if args.with_polls else None
                )
                try:
                    steady = run_phase(
                        executor, heartbeat, args.daemons * args.cycles, args.daemons / args.interval, args, steady=True
                    )
                finally:
                    stop.set()
                print(json.dumps({"heartbeat": steady}), flush=True)
                if polls:
                    result = polls.result()
                    print(json.dumps({"polls": result}), flush=True)
                    return steady["passed"] and result["passed"]
                return steady["passed"]
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-a", type=service_origin, required=True)
    parser.add_argument("--origin-b", type=service_origin, required=True)
    parser.add_argument("--tokens", type=Path, required=True, help="JSON scope-to-token map")
    parser.add_argument("--scope", help="Token scope, defaults to the first scope in the file")
    parser.add_argument("--ca", type=Path, help="CA certificate for the HTTPS origins")
    parser.add_argument("--daemons", type=int, default=1000)
    parser.add_argument("--interval", type=float, default=15, help="Heartbeat interval in seconds")
    parser.add_argument("--cycles", type=int, default=4, help="Number of heartbeat intervals")
    parser.add_argument("--registration-rps", type=float, default=500)
    parser.add_argument("--workers", type=int, default=64)
    parser.add_argument("--max-lag-ms", type=float, default=500, help="Maximum client scheduling lag")
    parser.add_argument("--max-p99-ms", type=float, default=1000, help="Local smoke-test request p99 limit")
    parser.add_argument("--with-polls", action="store_true", help="Run concurrent daemon long polls during heartbeats")
    args = parser.parse_args()
    if args.origin_a.netloc == args.origin_b.netloc:
        parser.error("Supply two different replica origins")
    if not (1 <= args.daemons <= 12500 and 1 <= args.cycles <= 100 and 1 <= args.workers <= 256):
        parser.error("Use 1–12500 daemons, 1–100 cycles and 1–256 workers")
    if not all(
        math.isfinite(value) and value > 0
        for value in (args.interval, args.registration_rps, args.max_lag_ms, args.max_p99_ms)
    ):
        parser.error("Rates, intervals and latency limits must be finite and positive")
    tokens = json.loads(args.tokens.read_text(encoding="utf-8"))
    if not isinstance(tokens, dict) or not tokens:
        parser.error("The token file must map scopes to Bearer Tokens")
    token = tokens.get(args.scope or next(iter(tokens)))
    if isinstance(token, list) and token:
        token = token[0]
    if not isinstance(token, str) or not token.isascii() or not token.isprintable() or not token:
        parser.error("The requested scope must contain a Bearer Token")
    passed = run(args, token)
    print("PASS: synthetic I8 load" if passed else "FAIL: synthetic I8 load")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
