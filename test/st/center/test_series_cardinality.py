"""Write and verify synthetic active series on an isolated VictoriaMetrics test tenant."""

import argparse
import concurrent.futures
import http.client
import json
import math
import secrets
import ssl
import struct
import sys
import threading
import time
from pathlib import Path
from urllib.parse import urlencode

from remote_write_load import field, label, load_token, snappy_block, validate_transport, varint


METRIC = "msmonitor_i3_cardinality_sample"
ACTIVE_WINDOW_SECONDS = 3600
IO_TIMEOUT = 10


def connection(args, tls):
    if tls is None:
        return http.client.HTTPConnection(args.host, args.port, timeout=IO_TIMEOUT)
    return http.client.HTTPSConnection(args.host, args.port, timeout=IO_TIMEOUT, context=tls)


def batch_body(run_id, group, start, stop, sample):
    common = label("__name__", METRIC) + label("run_id", run_id) + label("group", str(group))
    return snappy_block(
        b"".join(field(1, common + label("series", str(index)) + sample) for index in range(start, stop))
    )


class Pace:
    """Share a batch dispatch rate across writers."""

    def __init__(self, rate):
        self.rate = rate
        self.next_send = 0.0
        self.lock = threading.Lock()
        self.stop = threading.Event()

    def wait(self, size, deadline):
        with self.lock:
            now = time.monotonic()
            scheduled = max(now, self.next_send)
            if scheduled >= deadline:
                raise RuntimeError("Write time budget exhausted")
            if self.stop.wait(scheduled - now):
                return False
            self.next_send = time.monotonic() + size / self.rate
            return True


def write_series(args, tls, token, run_id, timestamp, groups, deadline):
    path = f"/insert/{args.tenant}/prometheus/api/v1/write"
    headers = {
        "Authorization": "Bearer " + token,
        "User-Agent": "msmonitor-i3-cardinality/1.0",
        "Content-Type": "application/x-protobuf",
        "Content-Encoding": "snappy",
        "X-Prometheus-Remote-Write-Version": "0.1.0",
    }
    sample = field(2, b"\x09" + struct.pack("<d", 1.0) + b"\x10" + varint(timestamp))
    pace = Pace(args.max_series_per_second)
    workers = min(args.workers, groups)

    def write_worker(worker):
        conn = connection(args, tls)
        accepted = body_bytes = 0
        group = worker
        try:
            for group in range(worker, groups, workers):
                start = group * args.group_size
                stop = min(start + args.group_size, args.series)
                for batch_start in range(start, stop, args.batch_size):
                    if pace.stop.is_set():
                        return accepted, body_bytes, None
                    batch_stop = min(batch_start + args.batch_size, stop)
                    size = batch_stop - batch_start
                    body = batch_body(run_id, group, batch_start, batch_stop, sample)
                    if not pace.wait(size, deadline):
                        return accepted, body_bytes, None
                    conn.request("POST", path, body=body, headers=headers)
                    with conn.getresponse() as response:
                        if response.status != 204:
                            raise RuntimeError(f"Write returned HTTP {response.status}")
                        response.read(1)
                    accepted += size
                    body_bytes += len(body)
                if (group + 1) % 25 == 0:
                    print(f"Completed write group {group + 1}/{groups}", file=sys.stderr, flush=True)
        except (OSError, RuntimeError, http.client.HTTPException) as error:
            pace.stop.set()
            return accepted, body_bytes, f"group {group}: {error}"
        finally:
            conn.close()
        return accepted, body_bytes, None

    interrupted = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(write_worker, worker) for worker in range(workers)]
        try:
            results = [future.result() for future in futures]
        except KeyboardInterrupt:
            pace.stop.set()
            interrupted = True
            results = [future.result() for future in futures]
    return (
        sum(result[0] for result in results),
        sum(result[1] for result in results),
        [result[2] for result in results if result[2]],
        interrupted,
    )


def query_count(conn, tenant, token, operation, run_id, group, timestamp):
    selector = f'{METRIC}{{run_id="{run_id}",group="{group}"}}'
    path = f"/select/{tenant}/prometheus/api/v1/query?" + urlencode(
        {
            "query": f"{operation}(count_over_time({selector}[1m]))",
            "time": f"{timestamp // 1000 + 1}.{timestamp % 1000:03}",
            "latency_offset": "1ms",
            "nocache": "1",
            "deny_partial_response": "1",
            "timeout": "5s",
        }
    )
    conn.request("GET", path, headers={"Authorization": "Bearer " + token})
    with conn.getresponse() as response:
        if response.status != 200:
            raise RuntimeError(f"Query returned HTTP {response.status}")
        data = response.read(65537)
        if len(data) > 65536:
            raise RuntimeError("Query response exceeded 64 KiB")
    payload = json.loads(data)
    if payload.get("status") != "success" or payload.get("isPartial"):
        raise RuntimeError("Query failed or returned a partial response")
    data = payload["data"]
    rows = data["result"]
    if data["resultType"] != "vector" or len(rows) > 1:
        raise RuntimeError("Unexpected aggregate response")
    if not rows:
        return 0
    value = float(rows[0]["value"][1])
    if not value.is_integer() or value < 0:
        raise RuntimeError("Unexpected aggregate count")
    return int(value)


def verify(args, tls, token, run_id, timestamp, groups, deadline, result):
    conn = connection(args, tls)
    try:
        for group in range(groups):
            expected = min(args.group_size, args.series - group * args.group_size)
            observed = (0, 0)
            while time.monotonic() < deadline:
                observed = (
                    query_count(conn, args.tenant, token, "count", run_id, group, timestamp),
                    query_count(conn, args.tenant, token, "sum", run_id, group, timestamp),
                )
                if observed == (expected, expected):
                    break
                if max(observed) > expected:
                    raise RuntimeError(f"Group {group} expected {expected}, read {observed}")
                time.sleep(min(1.0, max(0.0, deadline - time.monotonic())))
            else:
                raise RuntimeError(f"Group {group} expected {expected}, read {observed} before timeout")
            result["verified_series"] += observed[0]
            result["verified_samples"] += observed[1]
            if (group + 1) % 25 == 0:
                print(f"Verified {result['verified_series']}/{args.series} series", file=sys.stderr, flush=True)
    finally:
        conn.close()


def run(args, tls, token, query_token):
    run_id = secrets.token_hex(8)
    groups = math.ceil(args.series / args.group_size)
    started = time.monotonic()
    timestamp = time.time_ns() // 1_000_000
    active_deadline = started + ACTIVE_WINDOW_SECONDS
    result = {
        "run_id": run_id,
        "tenant": args.tenant,
        "timestamp_ms": timestamp,
        "expected_series": args.series,
        "groups": groups,
        "max_series_per_second": args.max_series_per_second,
        "accepted_samples": 0,
        "verified_series": 0,
        "verified_samples": 0,
        "request_body_bytes": 0,
        "passed": False,
    }
    phase = "query preflight"
    interrupted = False
    try:
        conn = connection(args, tls)
        try:
            if query_count(conn, args.tenant, query_token, "count", run_id, 0, timestamp) != 0:
                raise RuntimeError("Fresh run ID already has samples")
        finally:
            conn.close()
        phase = "write"
        print(f"Writing {args.series} series in {groups} groups, run_id={run_id}", file=sys.stderr, flush=True)
        write_start = time.monotonic()
        accepted, body_bytes, errors, interrupted = write_series(
            args, tls, token, run_id, timestamp, groups, min(active_deadline, write_start + args.write_timeout)
        )
        result["write_elapsed_seconds"] = round(time.monotonic() - write_start, 3)
        result["accepted_samples"] = accepted
        result["request_body_bytes"] = body_bytes
        if interrupted:
            raise KeyboardInterrupt
        if errors or accepted != args.series:
            raise RuntimeError("; ".join(errors) or "Not all writes were accepted")
        phase = "readback"
        verify(
            args,
            tls,
            query_token,
            run_id,
            timestamp,
            groups,
            min(active_deadline, time.monotonic() + args.readback_timeout),
            result,
        )
        if time.monotonic() >= active_deadline:
            raise RuntimeError("Run exceeded the one-hour active series window")
        result["verified_rounds"] = 1
        for _ in range(1, args.rounds):
            # Keep the previous samples outside this round's one-minute verification window.
            time.sleep(max(0, (timestamp + 61000 - time.time_ns() // 1_000_000) / 1000))
            timestamp = time.time_ns() // 1_000_000
            accepted, size, errors, interrupted = write_series(
                args, tls, token, run_id, timestamp, groups, min(active_deadline, time.monotonic() + args.write_timeout)
            )
            if interrupted:
                raise KeyboardInterrupt
            if errors or accepted != args.series:
                raise RuntimeError("; ".join(errors) or "Incomplete series refresh")
            result["accepted_samples"] += accepted
            result["request_body_bytes"] += size
            result["verified_series"] = 0
            verify(
                args,
                tls,
                query_token,
                run_id,
                timestamp,
                groups,
                min(active_deadline, time.monotonic() + args.readback_timeout),
                result,
            )
            result["verified_rounds"] += 1
        result["passed"] = True
    except KeyboardInterrupt:
        interrupted = True
        result["error"] = f"{phase}: Interrupted"
    except (OSError, RuntimeError, http.client.HTTPException, ValueError, KeyError, IndexError, TypeError) as error:
        result["error"] = f"{phase}: {error}"
    result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    print(json.dumps(result, indent=2))
    return 130 if interrupted else int(not result["passed"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("series", type=int, help="Unique series to create (maximum 12000000)")
    parser.add_argument("--tenant", type=int, required=True, help="Account ID of the test tenant")
    parser.add_argument("--tokens", type=Path, required=True, help="JSON tenant-to-write-token mapping")
    parser.add_argument("--query-tokens", type=Path, help="Optional separate tenant-to-read-token mapping")
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--ca", type=Path, help="CA certificate for HTTPS")
    parser.add_argument("--group-size", type=int, default=10_000, help="Series per readback group (default 10000)")
    parser.add_argument("--batch-size", type=int, default=1000, help="Series per write request (default 1000)")
    parser.add_argument("--workers", type=int, default=4, help="Maximum concurrent writes (default 4)")
    parser.add_argument(
        "--max-series-per-second", type=int, default=20_000, help="Shared write dispatch rate (default 20000)"
    )
    parser.add_argument("--write-timeout", type=int, default=1800, help="Write time budget in seconds")
    parser.add_argument("--readback-timeout", type=int, default=600, help="Readback time budget in seconds")
    parser.add_argument(
        "--rounds", type=int, default=1, choices=range(1, 11), help="Refresh the same series in successive rounds"
    )
    args = parser.parse_args()
    if not (1 <= args.port <= 65535 and 0 <= args.tenant <= 2**32 - 1 and 1 <= args.series <= 12_000_000):
        parser.error("limits: port 1..65535, tenant 0..4294967295, series 1..12000000")
    if not (
        1 <= args.group_size <= 10_000
        and 1 <= args.batch_size <= 5000
        and math.ceil(args.series / args.group_size) <= 1200
        and 1 <= args.workers <= 16
    ):
        parser.error("limits: group size 1..10000, at most 1200 groups, batch size 1..5000, workers 1..16")
    if not (
        1 <= args.max_series_per_second <= 250_000
        and 1 <= args.write_timeout < ACTIVE_WINDOW_SECONDS
        and 1 <= args.readback_timeout < ACTIVE_WINDOW_SECONDS
    ):
        parser.error("invalid dispatch rate or timeout (each timeout must be below 3600 seconds)")
    if args.series / args.max_series_per_second > args.write_timeout:
        parser.error("configured dispatch rate cannot finish within the write timeout")
    validate_transport(parser, args)
    try:
        tls = ssl.create_default_context(cafile=args.ca) if args.tls else None
        token = load_token(args.tokens, args.tenant)
        query_token = load_token(args.query_tokens, args.tenant) if args.query_tokens else token
    except (OSError, KeyError, TypeError, ValueError):
        parser.error("Cannot load CA or tenant credential files")
    return run(args, tls, token, query_token)


if __name__ == "__main__":
    raise SystemExit(main())
