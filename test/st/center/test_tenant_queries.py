"""Smoke tenant-authorized queries and Grafana metadata endpoints."""

import argparse
import json
import secrets
import ssl
import struct
import time
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import HTTPSHandler, ProxyHandler, build_opener

from remote_write_load import field, label, load_token, snappy_block, varint
from daemon_smoke import NoRedirect, request
from test_control_service import service_origin


METRIC = "msmonitor_i4_probe"
GRAFANA_LABELS = ("rank", "comm_group", "range_name")
GRAFANA_ENDPOINTS = ("status/buildinfo", *(f"label/{name}/values" for name in GRAFANA_LABELS))
READ_ENDPOINTS = ("query", "query_range", "series", "labels", *GRAFANA_ENDPOINTS)
WRITE_HEADERS = {
    "Content-Type": "application/x-protobuf",
    "Content-Encoding": "snappy",
    "X-Prometheus-Remote-Write-Version": "0.1.0",
    "User-Agent": "msmonitor-i4-probe/1.0",
}


def read_path(tenant, endpoint, parameters):
    return f"/select/{tenant}/prometheus/api/v1/{endpoint}?{urlencode(parameters)}"


def write_path(tenant):
    return f"/insert/{tenant}/prometheus/api/v1/write"


def encode_batch(observations, start, run_id):
    batch = bytearray()
    for (job, rank), values in observations.items():
        timeseries = bytearray(
            label("__name__", METRIC) + label("job", job) + label("rank", rank) + label("run_id", run_id)
        )
        for offset, value in enumerate(values):
            timestamp = (start + offset) * 1000
            sample = b"\x09" + struct.pack("<d", value) + b"\x10" + varint(timestamp)
            timeseries.extend(field(2, sample))
        batch.extend(field(1, timeseries))
    return snappy_block(batch)


def read_data(opener, origin, tenant, endpoint, parameters, token):
    payload = json.loads(request(opener, origin, read_path(tenant, endpoint, parameters), token=token))
    if payload.get("status") != "success" or payload.get("isPartial"):
        raise RuntimeError(f"tenant {tenant}: unsuccessful or partial {endpoint} response")
    return payload["data"]


def verify_queries(opener, origin, tenant, token, observations, start, run_id, timeout):
    expected_totals = {}
    for (job, _), values in observations.items():
        expected_totals[job] = tuple(left + right for left, right in zip(expected_totals.get(job, (0, 0)), values))
    selector = f'{METRIC}{{run_id="{run_id}"}}'
    query = {"query": f"sum by (job) ({selector})", "nocache": "1", "deny_partial_response": "1"}
    instant_query = {**query, "time": start + 1}
    expected_vector = sorted((job, values[-1]) for job, values in expected_totals.items())
    deadline = time.monotonic() + timeout
    while True:
        result = read_data(opener, origin, tenant, "query", instant_query, token)
        vector = sorted((row["metric"]["job"], float(row["value"][1])) for row in result["result"])
        if result["resultType"] == "vector" and vector == expected_vector:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError(f"tenant {tenant}: instant query expected {expected_vector}, got {vector}")
        time.sleep(1)

    range_query = {**query, "start": start, "end": start + 1, "step": "1s"}
    result = read_data(opener, origin, tenant, "query_range", range_query, token)
    matrix = sorted(
        (row["metric"]["job"], [(point[0], float(point[1])) for point in row["values"]]) for row in result["result"]
    )
    expected_matrix = sorted(
        (job, [(start, values[0]), (start + 1, values[1])]) for job, values in expected_totals.items()
    )
    if result["resultType"] != "matrix" or matrix != expected_matrix:
        raise RuntimeError(f"tenant {tenant}: range query expected {expected_matrix}, got {matrix}")

    metadata_query = {"match[]": selector, "start": start - 1, "end": start + 2, "deny_partial_response": "1"}
    series = read_data(opener, origin, tenant, "series", metadata_query, token)
    expected_series = [
        {"__name__": METRIC, "job": job, "rank": rank, "run_id": run_id} for job, rank in sorted(observations)
    ]
    if sorted(series, key=lambda item: (item["job"], item["rank"])) != expected_series:
        raise RuntimeError(f"tenant {tenant}: unexpected series: {series!r}")
    labels = read_data(opener, origin, tenant, "labels", metadata_query, token)
    if set(labels) != {"__name__", "job", "rank", "run_id"}:
        raise RuntimeError(f"tenant {tenant}: unexpected label names: {labels!r}")
    for name in GRAFANA_LABELS:
        values = read_data(opener, origin, tenant, f"label/{name}/values", metadata_query, token)
        expected = {rank for _, rank in observations} if name == "rank" else set()
        if not isinstance(values, list) or set(values) != expected:
            raise RuntimeError(f"tenant {tenant}: unexpected {name} values: {values!r}")
    if not read_data(opener, origin, tenant, "status/buildinfo", {}, token).get("version"):
        raise RuntimeError(f"tenant {tenant}: missing build info version")
    print(f"PASS tenant={tenant}: status/buildinfo and label values for rank, comm_group, range_name")


def verify_denials(opener, write_origin, query_origin, tenants, write_tokens, query_tokens, sample_body, run_id):
    for tenant, other in (tenants, tenants[::-1]):
        # vmauth returns 400 for an authenticated request outside the configured paths.
        request(
            opener,
            write_origin,
            write_path(tenant),
            expected=400,
            token=query_tokens[tenant],
            body=sample_body,
            headers=WRITE_HEADERS,
        )
        request(
            opener,
            write_origin,
            write_path(other),
            expected=400,
            token=write_tokens[tenant],
            body=sample_body,
            headers=WRITE_HEADERS,
        )
        selector = f'{METRIC}{{run_id="{run_id}"}}'
        now = int(time.time())
        parameters = {"query": selector, "match[]": selector, "start": now - 60, "end": now, "step": "1s"}
        for endpoint in READ_ENDPOINTS:
            own_path = read_path(tenant, endpoint, parameters)
            other_path = read_path(other, endpoint, parameters)
            request(opener, query_origin, own_path, expected=400, token=write_tokens[tenant])
            request(opener, query_origin, other_path, expected=400, token=query_tokens[tenant])
            request(opener, query_origin, own_path, expected=401)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", type=service_origin, required=True, help="Write HTTPS or loopback HTTP origin")
    parser.add_argument("--query-origin", type=service_origin, help="Separate read origin, if configured")
    parser.add_argument("--ca", type=Path, help="CA certificate for test HTTPS origins")
    parser.add_argument("--write-tokens", type=Path, required=True, help="JSON tenant-to-write-token map")
    parser.add_argument("--query-tokens", type=Path, required=True, help="JSON tenant-to-read-token map")
    parser.add_argument("--tenants", nargs=2, type=int, default=(10, 20), metavar=("A", "B"))
    parser.add_argument("--timeout", type=int, default=60, help="Maximum seconds to wait for each tenant's readback")
    args = parser.parse_args()
    tenants = tuple(args.tenants)
    if tenants[0] == tenants[1] or not all(0 <= tenant < 2**32 for tenant in tenants):
        parser.error("Provide two distinct uint32 tenant IDs")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        write_tokens = {tenant: load_token(args.write_tokens, tenant) for tenant in tenants}
        query_tokens = {tenant: load_token(args.query_tokens, tenant) for tenant in tenants}
    except (KeyError, OSError, ValueError):
        parser.error("Provide readable JSON maps containing valid Bearer tokens for both tenants")
    if len(set(write_tokens.values()) | set(query_tokens.values())) != 4:
        parser.error("Each tenant and role must have a distinct test credential")

    tls = ssl.create_default_context(cafile=str(args.ca) if args.ca else None)
    opener = build_opener(ProxyHandler({}), NoRedirect(), HTTPSHandler(context=tls))
    write_origin = args.origin.geturl().rstrip("/")
    query_origin = (args.query_origin or args.origin).geturl().rstrip("/")
    run_id = secrets.token_hex(8)
    # Samples have two whole-second slots outside the default query latency offset.
    start = int(time.time()) - 60
    observations = {
        tenants[0]: {("i4-job-a", "0"): (2, 3), ("i4-job-a", "1"): (3, 4), ("i4-job-b", "0"): (11, 12)},
        tenants[1]: {("i4-job-a", "0"): (90, 91)},
    }
    print(f"I4 probe run_id={run_id}", flush=True)
    bodies = {tenant: encode_batch(rows, start, run_id) for tenant, rows in observations.items()}
    for tenant, body in bodies.items():
        request(
            opener,
            write_origin,
            write_path(tenant),
            expected=204,
            token=write_tokens[tenant],
            body=body,
            headers=WRITE_HEADERS,
        )
    verify_denials(opener, write_origin, query_origin, tenants, write_tokens, query_tokens, bodies[tenants[0]], run_id)
    for tenant, rows in observations.items():
        verify_queries(opener, query_origin, tenant, query_tokens[tenant], rows, start, run_id, args.timeout)
    print("PASS I4: both tenants, job aggregation, query, query_range, series, labels and role isolation")


if __name__ == "__main__":
    main()
