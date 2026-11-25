"""Send periodic Remote Write samples and verify raw I3 readback."""

import argparse
import collections
import http.client
import json
import math
import queue
import secrets
import ssl
import struct
import sys
import threading
import time
from pathlib import Path
from urllib.parse import urlencode

from remote_write_load import field, label, load_token, snappy_block, varint
from test_control_service import service_origin


METRIC = "msmonitor_i3_canary"
IO_TIMEOUT = 5
MAX_QUERY_BODY = 1024 * 1024


def connection(origin, tls):
    if origin.scheme == "https":
        return http.client.HTTPSConnection(origin.hostname, origin.port, timeout=IO_TIMEOUT, context=tls)
    return http.client.HTTPConnection(origin.hostname, origin.port, timeout=IO_TIMEOUT)


def write_samples(args, tls, token, run_id, outcomes):
    conn = connection(args.origin, tls)
    path = f"/insert/{args.tenant}/prometheus/api/v1/write"
    headers = {
        "Authorization": "Bearer " + token,
        "Content-Type": "application/x-protobuf",
        "Content-Encoding": "snappy",
        "X-Prometheus-Remote-Write-Version": "0.1.0",
        "User-Agent": "msmonitor-i3-canary/1.0",
    }
    labels = label("__name__", METRIC) + label("run_id", run_id)
    start = time.monotonic()
    last_timestamp = 0
    try:
        for slot in range(math.ceil(args.duration / args.interval)):
            scheduled = start + slot * args.interval
            time.sleep(max(0, scheduled - time.monotonic()))
            if time.monotonic() >= min(scheduled + args.interval, start + args.duration):
                outcomes.put(("skipped", slot + 1, None, None))
                continue
            seq = slot + 1
            timestamp = max(last_timestamp + 1, time.time_ns() // 1_000_000)
            last_timestamp = timestamp
            sample = field(2, b"\x09" + struct.pack("<d", seq) + b"\x10" + varint(timestamp))
            try:
                conn.request("POST", path, body=snappy_block(field(1, labels + sample)), headers=headers)
                with conn.getresponse() as response:
                    status = f"HTTP {response.status}"
                if status != "HTTP 204":
                    conn.close()
            except (OSError, http.client.HTTPException) as error:
                status = type(error).__name__
                conn.close()
            outcomes.put((status, seq, timestamp, time.monotonic()))
    finally:
        conn.close()
        outcomes.put(None)


def read_samples(conn, args, token, run_id, first_timestamp, last_timestamp):
    # Evaluate at a fixed wall-clock time and return raw samples, not interpolated points.
    end = last_timestamp + 1
    lookback = end - first_timestamp + 1
    params = urlencode(
        {
            "query": f'{METRIC}{{run_id="{run_id}"}}[{lookback}ms]',
            "time": f"{end // 1000}.{end % 1000:03d}",
            "latency_offset": "1ms",
            "nocache": "1",
            "deny_partial_response": "1",
            "timeout": "5s",
        }
    )
    path = f"/select/{args.tenant}/prometheus/api/v1/query?{params}"
    conn.request("GET", path, headers={"Authorization": "Bearer " + token})
    with conn.getresponse() as response:
        if response.status != 200:
            return f"HTTP {response.status}", {}
        body = response.read(MAX_QUERY_BODY + 1)
        if len(body) > MAX_QUERY_BODY:
            raise ValueError("Query returned more than 1 MiB")
    payload = json.loads(body)
    if payload.get("status") != "success" or payload.get("isPartial"):
        raise ValueError("Query was unsuccessful or partial")
    data = payload["data"]
    rows = data["result"]
    if data["resultType"] != "matrix" or len(rows) > 1:
        raise ValueError("Unexpected query result")
    if not rows:
        return "HTTP 200", {}
    row = rows[0]
    if row["metric"] != {"__name__": METRIC, "run_id": run_id}:
        raise ValueError("Unexpected series labels")
    samples = {round(stamp * 1000): float(value) for stamp, value in row["values"]}
    if len(samples) != len(row["values"]):
        raise ValueError("Duplicate sample timestamp")
    return "HTTP 200", samples


def run(args, tls, write_token, query_token):
    run_id = secrets.token_hex(8)
    conn = connection(args.origin, tls)
    outcomes = queue.SimpleQueue()
    pending = {}
    sent = {}
    writes = collections.Counter()
    queries = collections.Counter()
    latencies = []
    unseen = value_mismatches = 0
    issues = []
    writer_done = False
    last_query_error = None

    def emit(event, **values):
        print(
            json.dumps({"event": event, "run_id": run_id, "event_unix_ms": time.time_ns() // 1_000_000, **values}),
            file=sys.stderr,
            flush=True,
        )

    def record(outcome, seq, timestamp):
        issue = {"outcome": outcome, "seq": seq, "timestamp_ms": timestamp}
        emit("sample_issue", **issue)
        if len(issues) < 32:
            issues.append(issue)

    def accept(outcome):
        nonlocal writer_done
        if outcome is None:
            writer_done = True
            return
        status, seq, timestamp, finished = outcome
        writes[status] += 1
        if timestamp is not None:
            sent[timestamp] = (seq, status)
        if status == "HTTP 204":
            pending[timestamp] = (seq, finished)
        else:
            record(status, seq, timestamp)

    planned = math.ceil(args.duration / args.interval)
    final_verified = final_failed_writes_visible = final_unexpected = 0
    final_query_outcome = None
    emit("started", planned=planned, duration_seconds=args.duration)
    writer = threading.Thread(target=write_samples, args=(args, tls, write_token, run_id, outcomes), daemon=True)
    writer.start()
    try:
        while pending or not writer_done:
            next_query = time.monotonic() + args.poll_interval if pending else None
            while not writer_done:
                try:
                    outcome = outcomes.get(timeout=max(0, next_query - time.monotonic()) if next_query else None)
                except queue.Empty:
                    break
                accept(outcome)
                if pending and next_query is None:
                    next_query = time.monotonic()
                if next_query is not None and time.monotonic() >= next_query:
                    break
            if writer_done and next_query is not None:
                time.sleep(max(0, next_query - time.monotonic()))
            if not pending:
                continue

            try:
                status, samples = read_samples(conn, args, query_token, run_id, min(pending), max(pending))
            except (OSError, http.client.HTTPException, ValueError, KeyError, TypeError, OverflowError) as error:
                status, samples = type(error).__name__, {}
            queries[status] += 1
            if status != "HTTP 200":
                conn.close()
                last_query_error = status
                if queries[status] <= 3:
                    emit("query_error", outcome=status)
            observed = time.monotonic()
            for timestamp, (seq, accepted_at) in list(pending.items()):
                if timestamp in samples and samples[timestamp] != seq:
                    value_mismatches += 1
                    record("readback_value_mismatch", seq, timestamp)
                elif observed - accepted_at > args.visibility_timeout:
                    unseen += 1
                    record("visibility_timeout", seq, timestamp)
                elif timestamp in samples:
                    latency = (observed - accepted_at) * 1000
                    emit(
                        "first_visible" if not latencies else "sample_visible",
                        seq=seq,
                        timestamp_ms=timestamp,
                        visibility_ms=round(latency, 2),
                    )
                    latencies.append(latency)
                else:
                    continue
                del pending[timestamp]
        if writes["HTTP 204"]:
            try:
                final_query_outcome, samples = read_samples(conn, args, query_token, run_id, min(sent), max(sent))
            except (OSError, http.client.HTTPException, ValueError, KeyError, TypeError, OverflowError) as error:
                final_query_outcome, samples = type(error).__name__, {}
            for timestamp, value in samples.items():
                sample = sent.get(timestamp)
                if sample is None or sample[0] != value:
                    final_unexpected += 1
                elif sample[1] == "HTTP 204":
                    final_verified += 1
                else:
                    final_failed_writes_visible += 1
            emit(
                "final_readback",
                query_outcome=final_query_outcome,
                verified=final_verified,
                missing=writes["HTTP 204"] - final_verified,
                unexpected=final_unexpected,
                failed_writes_visible=final_failed_writes_visible,
            )
    finally:
        conn.close()

    accepted = writes["HTTP 204"]
    failed = sum(writes.values()) - writes["skipped"] - accepted
    read_failures = sum(queries.values()) - queries["HTTP 200"]
    latencies.sort()
    passed = (
        bool(latencies)
        and sum(writes.values()) == planned
        and failed <= args.max_write_failures
        and writes["skipped"] <= args.max_skipped
        and unseen <= args.max_unseen
        and read_failures <= args.max_read_failures
        and not value_mismatches
        and final_query_outcome == "HTTP 200"
        and final_verified == accepted
        and not final_unexpected
    )
    report = {
        "passed": passed,
        "run_id": run_id,
        "tenant": args.tenant,
        "duration_seconds": args.duration,
        "write_interval_seconds": args.interval,
        "planned": planned,
        "attempted": accepted + failed,
        "accepted": accepted,
        "visible": len(latencies),
        "accepted_unseen": unseen,
        "final_query_outcome": final_query_outcome,
        "final_verified": final_verified,
        "final_missing": accepted - final_verified,
        "final_unexpected": final_unexpected,
        "final_failed_writes_visible": final_failed_writes_visible,
        "write_failures": failed,
        "read_failures": read_failures,
        "skipped": writes["skipped"],
        "value_mismatches": value_mismatches,
        "write_outcomes": dict(writes),
        "query_outcomes": dict(queries),
        "last_query_error": last_query_error,
        "visibility_timeout_seconds": args.visibility_timeout,
        "visibility_p95_ms": round(latencies[math.ceil(len(latencies) * 0.95) - 1], 2) if latencies else None,
        "visibility_max_ms": round(latencies[-1], 2) if latencies else None,
        "issues_first_32": issues,
    }
    print(json.dumps(report, indent=2))
    return int(not passed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", required=True, type=service_origin)
    parser.add_argument("--tenant", type=int, required=True)
    parser.add_argument("--write-tokens", required=True, type=Path)
    parser.add_argument("--query-tokens", required=True, type=Path)
    parser.add_argument("--ca", type=Path, help="CA file for a private HTTPS endpoint")
    parser.add_argument("--interval", type=float, default=2, help="Seconds between writes")
    parser.add_argument("--duration", type=int, default=60, help="Write duration in seconds")
    parser.add_argument("--poll-interval", type=float, default=1, help="Seconds between reader polls")
    parser.add_argument("--visibility-timeout", type=int, default=30, help="Seconds allowed after HTTP 204")
    parser.add_argument("--max-write-failures", type=int, default=0)
    parser.add_argument("--max-unseen", type=int, default=0)
    parser.add_argument(
        "--max-read-failures", type=int, default=0, help="Allow failed query attempts during a fault drill (default: 0)"
    )
    parser.add_argument("--max-skipped", type=int, default=0, help="Allow missed send slots during a fault drill")
    args = parser.parse_args()
    if not (
        0 <= args.tenant <= 2**32 - 1
        and 1 <= args.duration <= 3600
        and 0.25 <= args.interval <= args.duration
        and 0.25 <= args.poll_interval <= 10
        and 5 <= args.visibility_timeout <= 180
    ):
        parser.error(
            "limits: tenant 0..4294967295, duration 1..3600, interval 0.25..duration, "
            "poll interval 0.25..10 and visibility timeout 5..180"
        )
    if min(args.max_write_failures, args.max_unseen, args.max_read_failures, args.max_skipped) < 0:
        parser.error("Failure thresholds must be nonnegative")
    if args.ca and args.origin.scheme != "https":
        parser.error("--ca requires HTTPS")
    try:
        tls = ssl.create_default_context(cafile=args.ca) if args.origin.scheme == "https" else None
        write_token = load_token(args.write_tokens, args.tenant)
        query_token = load_token(args.query_tokens, args.tenant)
    except (OSError, KeyError, TypeError, ValueError):
        parser.error("Cannot load CA or tenant credential files")
    return run(args, tls, write_token, query_token)


if __name__ == "__main__":
    raise SystemExit(main())
