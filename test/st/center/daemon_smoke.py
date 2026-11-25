"""Exercise the center's two HTTPS endpoints as a simulated daemon."""

import argparse
import json
import secrets
import ssl
import struct
import time
from pathlib import Path
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import HTTPRedirectHandler, HTTPSHandler, Request, build_opener

from remote_write_load import field, label, snappy_block, varint
from test_control_service import service_origin


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def request(opener, base, path, *, expected=200, token=None, body=None, headers=None):
    headers = dict(headers or {})
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    req = Request(base + path, data=body, headers=headers)
    try:
        response = opener.open(req, timeout=5)
    except HTTPError as error:
        response = error
    with response:
        if response.status != expected:
            raise RuntimeError(f"{path.split('?')[0]}: expected HTTP {expected}, got {response.status}")
        return response.read()


def verify_control(opener, base, token, run_id):
    def post(endpoint, data, credential, expected=200):
        return request(
            opener,
            base,
            "/api/v1/daemon/" + endpoint,
            token=credential,
            body=json.dumps(data).encode(),
            expected=expected,
            headers={"Content-Type": "application/json"},
        )

    identity = {"daemon_id": "smoke-" + run_id, "instance_id": "boot-1"}
    post("register", identity, None, expected=401)
    registration = json.loads(post("register", identity, token))
    session = registration["session_id"]
    if json.loads(post("register", identity, token))["session_id"] != session:
        raise RuntimeError("Registration retry returned a different session")
    heartbeat = {"session_id": session, "status_seq": 1, "collect_state": "running"}
    if json.loads(post("heartbeat", heartbeat, token)).get("accepted") is not True:
        raise RuntimeError("Heartbeat was not accepted")
    post("register", {**identity, "instance_id": "boot-2"}, token)
    post("heartbeat", heartbeat, token, expected=409)
    print("PASS I8: authentication, registration retry, heartbeat, replaced session")


def verify_write(opener, base, tenant, token, run_id, *, query_token=None):
    values = {"msmonitor_smoke_step": 1.0, "msmonitor_smoke_summary_p99": 2.0, "msmonitor_smoke_alert": 3.0}
    timestamp = int(time.time() * 1000)
    batch = bytearray()
    for metric, value in values.items():
        labels = label("__name__", metric) + label("run_id", run_id)
        sample = field(2, b"\x09" + struct.pack("<d", value) + b"\x10" + varint(timestamp))
        batch.extend(field(1, labels + sample))

    write = f"/insert/{tenant}/prometheus/api/v1/write"
    request(
        opener,
        base,
        write,
        expected=204,
        token=token,
        body=snappy_block(batch),
        headers={
            "Content-Type": "application/x-protobuf",
            "Content-Encoding": "snappy",
            "X-Prometheus-Remote-Write-Version": "0.1.0",
            "User-Agent": "msmonitor-daemon-smoke/1.0",
        },
    )
    # Query around the written sample and use the minimum accepted offset.
    query = f"/select/{tenant}/prometheus/api/v1/query?" + urlencode(
        {
            "query": f'last_over_time({{run_id="{run_id}"}}[1m]) keep_metric_names',
            "time": f"{timestamp // 1000 + 1}.{timestamp % 1000:03}",
            "latency_offset": "1ms",
            "nocache": "1",
            "deny_partial_response": "1",
        }
    )
    deadline = time.monotonic() + 60
    while True:
        payload = json.loads(request(opener, base, query, token=query_token or token))
        result = payload["data"]["result"]
        observed = {item["metric"]["__name__"]: float(item["value"][1]) for item in result}
        if not payload.get("isPartial") and len(result) == len(values) and observed == values:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError(
                f"I3 write accepted (HTTP 204), readback timed out for run_id={run_id}, "
                f"timestamp_ms={timestamp}, observed={observed!r}"
            )
        time.sleep(1)
    print("PASS I3: Remote Write and readback for metric, summary and alert samples")


def https_origin(value):
    origin = service_origin(value)
    if origin.scheme != "https":
        raise argparse.ArgumentTypeError("Expected an HTTPS origin, for example https://host:18469")
    return origin.geturl().rstrip("/")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-origin", required=True, type=https_origin)
    parser.add_argument("--control-origin", required=True, type=https_origin)
    parser.add_argument("--ca", required=True, type=Path)
    parser.add_argument("--write-tokens", required=True, type=Path)
    parser.add_argument("--query-tokens", type=Path, help="Separate read credential file for I3 verification")
    parser.add_argument("--control-tokens", required=True, type=Path)
    parser.add_argument("--tenant", default=10, type=int)
    parser.add_argument("--scope", default="demo-job")
    args = parser.parse_args()
    context = ssl.create_default_context(cafile=str(args.ca))
    opener = build_opener(NoRedirect(), HTTPSHandler(context=context))
    run_id = secrets.token_hex(8)
    print(f"run_id={run_id}", flush=True)
    control = json.loads(args.control_tokens.read_text(encoding="utf-8"))[args.scope]
    write = json.loads(args.write_tokens.read_text(encoding="utf-8"))[str(args.tenant)]
    query = json.loads(args.query_tokens.read_text(encoding="utf-8"))[str(args.tenant)] if args.query_tokens else None
    verify_control(opener, args.control_origin, control, run_id)
    verify_write(opener, args.write_origin, args.tenant, write, run_id, query_token=query)


if __name__ == "__main__":
    main()
