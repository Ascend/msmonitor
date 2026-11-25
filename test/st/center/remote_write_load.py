import argparse
import asyncio
import collections
import http.client
import ipaddress
import json
import math
import re
import ssl
import struct
import time
import uuid
from pathlib import Path
from urllib.parse import urlencode


METRIC = "msmonitor_req2_load_sample"
RAMP_SECONDS = 10.0


def varint(number):
    out = bytearray()
    while number >= 128:
        out.append((number & 127) | 128)
        number >>= 7
    out.append(number)
    return bytes(out)


def field(number, data):
    return varint(number * 8 + 2) + varint(len(data)) + data


def label(name, value):
    return field(1, field(1, name.encode()) + field(2, value.encode()))


def snappy_block(data):
    """Encode a raw Snappy block using one literal."""
    length = len(data) - 1
    if length < 60:
        tag = bytes([length << 2])
    else:
        width = (length.bit_length() + 7) // 8
        tag = bytes([(59 + width) << 2]) + length.to_bytes(width, "little")
    return varint(len(data)) + tag + data


def load_token(path, tenant):
    token = json.loads(path.read_text(encoding="utf-8"))[str(tenant)]
    if not isinstance(token, str) or not re.fullmatch(r"[A-Za-z0-9._~+/=-]{1,4096}", token):
        raise ValueError("Invalid Bearer token format")
    return token


def validate_transport(parser, args):
    try:
        loopback = ipaddress.ip_address(args.host).is_loopback
    except ValueError:
        if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", args.host):
            parser.error("host must be an IP address or DNS name")
        loopback = args.host.lower() == "localhost"
    if (args.tokens or args.query_tokens) and not (args.tls or loopback):
        parser.error("Bearer credentials require HTTPS or a loopback address")
    if args.ca and not args.tls:
        parser.error("--ca requires --tls")


def verify_readback(args, tls, token, run_id, requests, started, ended):
    """Compare sample counts and values per series using VictoriaMetrics instant queries."""
    deadline = time.monotonic() + args.readback_timeout
    lookback = max(1, math.ceil(ended - started) + 1)
    matched = 0
    headers = {"Authorization": "Bearer " + token} if token else {}
    # Limit each response to about 2048 expected series, independent of batch size.
    chunk_size = min(128, 2048 // args.batch_size)
    for start in range(0, args.connections, chunk_size):
        clients = range(start, min(start + chunk_size, args.connections))
        client_match = "|".join(map(str, clients))
        selector = f'{METRIC}{{run_id="{run_id}",client=~"{client_match}"}}'
        query = f'count_values_over_time("sample_value",{selector}[{lookback}s])'
        expected = {
            (str(client), str(series), 1.0): requests[client] for client in clients for series in range(args.batch_size)
        }
        path = f"/select/{args.tenant}/prometheus/api/v1/query?" + urlencode(
            {
                "query": query,
                "time": ended,
                "nocache": "1",
                "deny_partial_response": "1",
                "latency_offset": "1ms",
            }
        )
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError(f"Readback timeout at client {start} ({matched} matched series)")
            timeout = min(remaining, 10)
            if tls:
                conn = http.client.HTTPSConnection(args.host, args.port, timeout=timeout, context=tls)
            else:
                conn = http.client.HTTPConnection(args.host, args.port, timeout=timeout)
            try:
                conn.request("GET", path, headers=headers)
                response = conn.getresponse()
                if response.status != 200:
                    raise RuntimeError(f"Readback HTTP {response.status}")
                raw = response.read(4 * 1024 * 1024 + 1)
                if len(raw) > 4 * 1024 * 1024:
                    raise RuntimeError("Readback response exceeded 4 MiB")
                payload = json.loads(raw)
            finally:
                conn.close()
            if payload.get("status") != "success":
                raise RuntimeError("Readback query did not succeed")
            rows = payload["data"]["result"]
            actual = {}
            for row in rows:
                labels = row["metric"]
                if labels.get("run_id") != run_id:
                    raise RuntimeError("Readback returned a different run ID")
                key = (labels["client"], labels["series"], float(labels["sample_value"]))
                if key in actual:
                    raise RuntimeError("Readback returned duplicate series")
                actual[key] = float(row["value"][1])
            if not payload.get("isPartial") and actual == expected:
                matched += len(expected)
                break
            if any(key not in expected or count > expected[key] for key, count in actual.items()):
                raise RuntimeError(f"Readback returned unexpected values or excess samples at client {start}")
            time.sleep(min(1.0, max(0.0, deadline - time.monotonic())))
    return matched


async def run(args, tls, token, query_token):
    loop = asyncio.get_running_loop()
    run_id = uuid.uuid4().hex[:12]
    period, ramp = args.period, RAMP_SECONDS
    start = loop.time()
    wall_started = time.time()
    deadline = start + ramp + args.seconds
    stats = collections.Counter()
    errors = collections.Counter()
    latencies = []
    requests = [0] * args.connections
    host = f"[{args.host}]" if ":" in args.host else args.host
    authorization = f"Authorization: Bearer {token}\r\n" if token else ""

    async def send(client):
        writer = None
        active = False
        try:
            await asyncio.sleep(ramp * client / args.connections)
            reader, writer = await asyncio.wait_for(asyncio.open_connection(args.host, args.port, ssl=tls), timeout=10)
            common = label("__name__", METRIC) + label("client", str(client)) + label("run_id", run_id)
            labels = [common + label("series", str(i)) for i in range(args.batch_size)]
            while loop.time() < deadline:
                sent_at = loop.time()
                timestamp = int(time.time() * 1000)
                sample = field(2, b"\x09" + struct.pack("<d", 1.0) + b"\x10" + varint(timestamp))
                body = snappy_block(b"".join(field(1, item + sample) for item in labels))
                header = (
                    f"POST /insert/{args.tenant}/prometheus/api/v1/write HTTP/1.1\r\n"
                    f"Host: {host}:{args.port}\r\n"
                    f"{authorization}"
                    "User-Agent: msmonitor-req2-load/1.0\r\n"
                    "Content-Type: application/x-protobuf\r\n"
                    "Content-Encoding: snappy\r\n"
                    "X-Prometheus-Remote-Write-Version: 0.1.0\r\n"
                    f"Content-Length: {len(body)}\r\n"
                    "Connection: keep-alive\r\n\r\n"
                ).encode()
                writer.write(header + body)
                await asyncio.wait_for(writer.drain(), timeout=10)
                response = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=10)
                lines = response.split(b"\r\n")
                code = int(lines[0].split()[1])
                if code != 204:
                    raise RuntimeError(f"HTTP {code}")
                if not active:
                    active = True
                    stats["active"] += 1
                    stats["peak_active"] = max(stats["peak_active"], stats["active"])
                    stats["clients_with_success"] += 1
                stats["requests_ok"] += 1
                stats["samples_ok"] += args.batch_size
                stats["body_bytes_ok"] += len(body)
                requests[client] += 1
                if sent_at >= start + ramp:
                    stats["steady_samples_ok"] += args.batch_size
                    stats["steady_body_bytes_ok"] += len(body)
                latencies.append((loop.time() - sent_at) * 1000)
                if any(line.lower() == b"connection: close" for line in lines[1:]):
                    raise RuntimeError("server closed keep-alive")
                await asyncio.sleep(max(0, min(sent_at + period, deadline) - loop.time()))
        except Exception as exc:
            errors[f"{type(exc).__name__}: {str(exc)[:100]}"] += 1
        finally:
            if active:
                stats["active"] -= 1
            if writer is not None:
                writer.close()
                try:
                    await asyncio.wait_for(writer.wait_closed(), timeout=2)
                except (OSError, asyncio.TimeoutError):
                    pass

    await asyncio.gather(*(send(client) for client in range(args.connections)))
    elapsed = loop.time() - start
    wall_ended = time.time()
    latencies.sort()
    p95 = latencies[math.ceil(0.95 * len(latencies)) - 1] if latencies else None
    steady_samples = stats["steady_samples_ok"] / args.seconds
    steady_body_bytes = stats["steady_body_bytes_ok"] / args.seconds
    if steady_samples < args.min_steady_samples_per_second:
        errors["Steady samples/s below requested minimum"] += 1
    if steady_body_bytes < args.min_steady_body_bytes_per_second:
        errors["Steady request-body bytes/s below requested minimum"] += 1
    passed = not errors and stats["peak_active"] == args.connections and stats["requests_ok"] > args.connections
    readback_series = None
    if args.verify_readback and passed:
        try:
            readback_series = verify_readback(args, tls, query_token, run_id, requests, wall_started, wall_ended)
        except (OSError, RuntimeError, ValueError, KeyError, http.client.HTTPException) as exc:
            errors[f"Readback {type(exc).__name__}: {str(exc)[:100]}"] += 1
            passed = False
    print(
        json.dumps(
            {
                "passed": passed,
                "run_id": run_id,
                "tenant": args.tenant,
                "target_connections": args.connections,
                "peak_active_connections": stats["peak_active"],
                "clients_with_success": stats["clients_with_success"],
                "requests_ok": stats["requests_ok"],
                "samples_ok": stats["samples_ok"],
                "target_series": args.connections * args.batch_size,
                "configured_period_seconds": period,
                "target_samples_per_second": round(args.connections * args.batch_size / period, 1),
                "steady_samples_per_second": round(steady_samples, 1),
                "steady_request_body_bytes_per_second": round(steady_body_bytes, 1),
                "min_steady_samples_per_second": args.min_steady_samples_per_second,
                "min_steady_body_bytes_per_second": args.min_steady_body_bytes_per_second,
                "samples_per_second": round(stats["samples_ok"] / elapsed, 1),
                "request_body_bytes_per_second": round(stats["body_bytes_ok"] / elapsed, 1),
                "http_p95_ms": round(p95, 2) if p95 is not None else None,
                "elapsed_seconds": round(elapsed, 2),
                "readback_verified_series": readback_series,
                "errors": dict(errors),
            },
            indent=2,
        )
    )
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description="Remote Write connection and sample load probe")
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("connections", type=int)
    parser.add_argument("seconds", type=int)
    parser.add_argument("batch_size", type=int)
    parser.add_argument("--tenant", type=int, default=90, help="VictoriaMetrics account ID (default: 90)")
    parser.add_argument("--period", type=float, default=5, help="Seconds between writes per client (default: 5)")
    parser.add_argument("--tls", action="store_true", help="Use HTTPS and verify the server certificate")
    parser.add_argument("--ca", type=Path, help="CA certificate for a private HTTPS endpoint")
    parser.add_argument("--tokens", type=Path, help="JSON file mapping tenant IDs to write Bearer tokens")
    parser.add_argument("--query-tokens", type=Path, help="Use a separate read token file for verification")
    parser.add_argument("--verify-readback", action="store_true", help="Query counts and values after writing")
    parser.add_argument("--readback-timeout", type=int, default=60, help="Readback time budget in seconds")
    parser.add_argument(
        "--min-steady-samples-per-second",
        type=float,
        default=0,
        help="Fail if measured steady-state samples/s is below this floor",
    )
    parser.add_argument(
        "--min-steady-body-bytes-per-second",
        type=float,
        default=0,
        help="Fail if measured steady-state Remote Write body bytes/s is below this floor",
    )
    args = parser.parse_args()
    if not (
        1 <= args.port <= 65535
        and 1 <= args.connections <= 30000
        and 10 <= args.seconds <= 600
        and 1 <= args.batch_size <= 128
    ):
        parser.error("limits: port 1..65535, connections 1..30000, seconds 10..600, batch_size 1..128")
    if not (0 <= args.tenant <= 2**32 - 1 and 0.1 <= args.period <= 5 and 1 <= args.readback_timeout <= 600):
        parser.error("limits: tenant 0..4294967295, period 0.1..5, readback timeout 1..600")
    if args.connections * (1 + math.ceil((RAMP_SECONDS + args.seconds) / args.period)) > 2_000_000:
        parser.error("limit: at most 2000000 requests per probe")
    floors = (args.min_steady_samples_per_second, args.min_steady_body_bytes_per_second)
    if any(not math.isfinite(floor) or floor < 0 for floor in floors):
        parser.error("steady-state rate floors must be finite and nonnegative")
    validate_transport(parser, args)
    if args.query_tokens and not args.verify_readback:
        parser.error("--query-tokens requires --verify-readback")
    try:
        tls = ssl.create_default_context(cafile=args.ca) if args.tls else None
        token = load_token(args.tokens, args.tenant) if args.tokens else None
        query_token = load_token(args.query_tokens, args.tenant) if args.query_tokens else token
    except (OSError, KeyError, TypeError, ValueError):
        parser.error("Cannot load CA or tenant credential files")
    return asyncio.run(run(args, tls, token, query_token))


if __name__ == "__main__":
    raise SystemExit(main())
