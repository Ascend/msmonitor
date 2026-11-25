"""I8 registration and heartbeat API for a shared PostgreSQL registry."""

import asyncio
import hashlib
import json
import logging
import os
import re
import time
import uuid
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from prometheus_client import CONTENT_TYPE_LATEST, REGISTRY, Counter, Histogram, generate_latest
from prometheus_client.core import CounterMetricFamily, GaugeMetricFamily
from starlette.requests import ClientDisconnect
from starlette.responses import JSONResponse, Response

from registry import Registry, RequestError, read_dsn


LOG = logging.getLogger("uvicorn.error")
HEARTBEAT_INTERVAL = 15
MAX_BODY = 4096
MAX_SEQUENCE = 2**63 - 1
ID_PATTERN = re.compile(r"[A-Za-z0-9_.-]{1,128}")
TOKEN_PATTERN = re.compile(r"[A-Za-z0-9._~+/=-]{32,256}")
REGISTRATION_IDS = frozenset(("job_id", "run_id", "node_id", "daemon_version"))
HEARTBEAT_COUNTERS = frozenset(("pending_files", "dropped_records", "rss_bytes"))
MAX_RANK_CAPABILITIES = 64
REQUESTS = Counter("msmonitor_i8_requests", "Handled I8 requests", ("operation", "status"))
DURATION = Histogram("msmonitor_i8_request_duration_seconds", "I8 request latency", ("operation",))


def load_credentials(path):
    scopes = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(scopes, dict) or not scopes:
        raise ValueError("Token file must map scopes to tokens")
    credentials = {}
    for scope, tokens in scopes.items():
        if not ID_PATTERN.fullmatch(scope):
            raise ValueError("Invalid authorization scope")
        if isinstance(tokens, str):
            tokens = [tokens]
        if not isinstance(tokens, list) or not 1 <= len(tokens) <= 2:
            raise ValueError("Each scope requires one token or two tokens during rotation")
        for token in tokens:
            if not isinstance(token, str) or not TOKEN_PATTERN.fullmatch(token):
                raise ValueError("Invalid Bearer Token format")
            digest = hashlib.sha256(token.encode("ascii")).digest()
            if digest in credentials:
                raise ValueError("Bearer Tokens must be unique")
            credentials[digest] = scope
    return credentials


def authorized_scope(request):
    values = request.headers.getlist("authorization")
    if len(values) == 1 and len(values[0]) <= 263:
        scheme, _, token = values[0].partition(" ")
        if scheme.lower() == "bearer" and TOKEN_PATTERN.fullmatch(token):
            scope = request.app.state.credentials.get(hashlib.sha256(token.encode("ascii")).digest())
            if scope is not None:
                return scope
    raise RequestError(401, "INVALID_CREDENTIALS")


async def read_json(request, limit=MAX_BODY):
    if request.headers.get("content-type", "").split(";", 1)[0].strip().lower() != "application/json":
        raise RequestError(400, "EXPECTED_JSON")
    length = request.headers.get("content-length")
    if length is not None:
        if not length.isascii() or not length.isdecimal() or len(length) > 8:
            raise RequestError(400, "INVALID_LENGTH")
        if int(length) > limit:
            raise RequestError(413, "BODY_TOO_LARGE")
    body = bytearray()
    try:
        async for chunk in request.stream():
            if len(body) + len(chunk) > limit:
                raise RequestError(413, "BODY_TOO_LARGE")
            body.extend(chunk)
    except ClientDisconnect:
        raise RequestError(400, "INCOMPLETE_BODY") from None
    try:
        data = json.loads(body.decode("utf-8"))
    except (ValueError, RecursionError):
        raise RequestError(400, "INVALID_JSON") from None
    if not isinstance(data, dict):
        raise RequestError(400, "EXPECTED_OBJECT")
    return data


def required_id(data, name):
    value = data.get(name)
    if not isinstance(value, str) or not ID_PATTERN.fullmatch(value):
        raise RequestError(400, "INVALID_" + name.upper())
    return value


def optional_metadata(data, allowed_fields):
    if "metadata" not in data:
        return None
    metadata = data["metadata"]
    if not isinstance(metadata, dict) or metadata.keys() - allowed_fields:
        raise RequestError(400, "INVALID_METADATA")
    return metadata


def registration_metadata(data):
    metadata = optional_metadata(data, REGISTRATION_IDS | {"rank_capabilities"})
    if metadata is None:
        return None
    for name in REGISTRATION_IDS & metadata.keys():
        required_id(metadata, name)
    if "rank_capabilities" in metadata:
        capabilities = metadata["rank_capabilities"]
        if (
            not isinstance(capabilities, list)
            or len(capabilities) > MAX_RANK_CAPABILITIES
            or not all(isinstance(item, str) and ID_PATTERN.fullmatch(item) for item in capabilities)
            or len(set(capabilities)) != len(capabilities)
        ):
            raise RequestError(400, "INVALID_RANK_CAPABILITIES")
    return metadata


def valid_counter(value):
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= MAX_SEQUENCE


def heartbeat_metadata(data):
    metadata = optional_metadata(data, HEARTBEAT_COUNTERS | {"app_version", "cpu_percent"})
    if metadata is None:
        return None
    if "app_version" in metadata:
        required_id(metadata, "app_version")
    for name in HEARTBEAT_COUNTERS & metadata.keys():
        value = metadata[name]
        if not valid_counter(value):
            raise RequestError(400, "INVALID_" + name.upper())
    if "cpu_percent" in metadata:
        value = metadata["cpu_percent"]
        if type(value) not in (int, float) or not 0 <= value <= 100:
            raise RequestError(400, "INVALID_CPU_PERCENT")
    return metadata


async def register(registry, scope, data):
    session_id = await registry.register(
        scope, required_id(data, "daemon_id"), required_id(data, "instance_id"), registration_metadata(data)
    )
    return {"session_id": session_id, "heartbeat_interval_s": HEARTBEAT_INTERVAL}


async def heartbeat(registry, scope, data):
    session = required_id(data, "session_id")
    state = required_id(data, "collect_state")
    seq = data.get("status_seq")
    if not valid_counter(seq):
        raise RequestError(400, "INVALID_STATUS_SEQ")
    await registry.heartbeat(scope, session, seq, state, heartbeat_metadata(data))
    return {"accepted": True}


async def poll_commands(registry, scope, data):
    session = required_id(data, "session_id")
    wait = data.get("wait_s", 20)
    if not valid_counter(wait) or wait > 20:
        raise RequestError(400, "INVALID_WAIT")
    deadline = time.monotonic() + wait
    while True:
        command = await registry.command_exchange(scope, session)
        remaining = deadline - time.monotonic()
        if command is not None or remaining <= 0:
            return {"command": command}
        # Release the shared database connection while the poll is idle.
        await asyncio.sleep(min(5, remaining))


async def ack_command(registry, scope, data):
    session = required_id(data, "session_id")
    request_id = required_id(data, "request_id")
    version = data.get("version")
    if not valid_counter(version) or version == 0:
        raise RequestError(400, "INVALID_VERSION")
    result = data.get("result")
    if not isinstance(result, dict) or result.keys() - {"rank", "status", "uri", "sha256", "missing_ranges_ns"}:
        raise RequestError(400, "INVALID_RESULT")
    if not valid_counter(result.get("rank")) or result.get("status") not in ("success", "partial", "failed"):
        raise RequestError(400, "INVALID_RESULT")
    if "uri" in result and (not isinstance(result["uri"], str) or not 1 <= len(result["uri"]) <= 1024):
        raise RequestError(400, "INVALID_URI")
    if "sha256" in result and (
        not isinstance(result["sha256"], str) or not re.fullmatch("[0-9a-f]{64}", result["sha256"])
    ):
        raise RequestError(400, "INVALID_SHA256")
    ranges = result.get("missing_ranges_ns", [])
    if not isinstance(ranges, list) or not all(
        isinstance(pair, list) and len(pair) == 2 and all(valid_counter(n) for n in pair) and pair[0] < pair[1]
        for pair in ranges
    ):
        raise RequestError(400, "INVALID_MISSING_RANGES")
    return await registry.command_exchange(scope, session, (request_id, version, result))


async def submit_summary(registry, scope, data):
    session = required_id(data, "session_id")
    batch = required_id(data, "batch_id")
    version = data.get("schema_version")
    if not isinstance(version, int) or isinstance(version, bool) or version != 1:
        raise RequestError(400, "UNSUPPORTED_SUMMARY_VERSION")
    rows = data.get("records")
    if (
        not isinstance(rows, list)
        or not 1 <= len(rows) <= 256
        or not all(isinstance(row, dict) and row for row in rows)
    ):
        raise RequestError(400, "INVALID_SUMMARY_RECORDS")
    try:
        records = json.dumps(rows, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("ascii")
    except (ValueError, RecursionError):
        raise RequestError(400, "INVALID_SUMMARY_RECORDS") from None
    await registry.submit_summary(scope, session, batch, version, records)
    return {"persisted": True, "batch_id": batch, "count": len(rows)}


async def handle(request, operation, limit=MAX_BODY):
    start = time.monotonic()
    request_id = uuid.uuid4().hex
    status = 500
    headers = {"Cache-Control": "no-store", "X-Request-ID": request_id}
    try:
        scope = authorized_scope(request)
        try:
            data = await asyncio.wait_for(read_json(request, limit), timeout=3)
        except asyncio.TimeoutError:
            raise RequestError(408, "BODY_TIMEOUT") from None
        if operation is poll_commands and data.get("wait_s", 20) and request.app.state.poll_slots.locked():
            # Check the session and queue once. Leave capacity for heartbeats and acknowledgements.
            if not valid_counter(data.get("wait_s", 20)) or data.get("wait_s", 20) > 20:
                raise RequestError(400, "INVALID_WAIT")
            payload = await operation(request.app.state.registry, scope, {**data, "wait_s": 0})
            if payload["command"] is None:
                payload["retry_after_s"] = HEARTBEAT_INTERVAL
        elif operation is poll_commands and data.get("wait_s", 20):
            async with request.app.state.poll_slots:
                payload = await operation(request.app.state.registry, scope, data)
        else:
            payload = await operation(request.app.state.registry, scope, data)
        if operation is register and scope in request.app.state.write_urls:
            payload["remote_write_url"] = request.app.state.write_urls[scope]
        status = 200
    except RequestError as error:
        status, payload = error.status, {"code": error.code}
        if status == 401:
            headers["WWW-Authenticate"] = "Bearer"
        if status == 503:
            headers["Retry-After"] = "1"
            if start >= request.app.state.next_error_log:
                request.app.state.next_error_log = start + 5
                LOG.warning(
                    json.dumps(
                        {
                            "request_id": request_id,
                            "operation": operation.__name__,
                            "status": status,
                            "code": error.code,
                        }
                    )
                )
    finally:
        REQUESTS.labels(operation.__name__, status).inc()
        DURATION.labels(operation.__name__).observe(time.monotonic() - start)
    return JSONResponse(payload, status_code=status, headers=headers)


class PoolMetrics:
    def __init__(self, registry):
        self.registry = registry

    def describe(self):
        return ()

    def collect(self):
        stats = self.registry.pool_stats()
        for name, help_text in (
            ("pool_size", "Current number of database connections"),
            ("pool_available", "Idle database connections"),
            ("requests_waiting", "Requests currently waiting for a database connection"),
        ):
            yield GaugeMetricFamily("msmonitor_i8_db_" + name, help_text, value=stats.get(name, 0))
        yield CounterMetricFamily(
            "msmonitor_i8_db_connection_wait_seconds",
            "Total time spent waiting for a database connection",
            value=stats.get("requests_wait_ms", 0) / 1000,
        )


@asynccontextmanager
async def lifespan(service):
    service.state.credentials = load_credentials(Path(os.environ["I8_TOKENS_FILE"]))
    service.state.write_urls = json.loads(os.getenv("I8_WRITE_URLS", "{}"))
    max_size = int(os.getenv("I8_DB_POOL_SIZE", "16"))
    if not 1 <= max_size <= 128:
        raise ValueError("I8_DB_POOL_SIZE must be in the range 1 to 128")
    registry = Registry(read_dsn(), max_size=max_size, timeout=1.0)
    await registry.open()
    service.state.registry = registry
    service.state.next_error_log = 0.0
    service.state.poll_slots = asyncio.Semaphore(64)
    pool_metrics = PoolMetrics(registry)
    REGISTRY.register(pool_metrics)
    LOG.info("I8 service started with database pool size %d", max_size)
    try:
        yield
    finally:
        REGISTRY.unregister(pool_metrics)
        await registry.close()


app = FastAPI(lifespan=lifespan, docs_url=None, redoc_url=None, openapi_url=None, redirect_slashes=False)


@app.post("/api/v1/daemon/register")
async def post_register(request: Request):
    return await handle(request, register)


@app.post("/api/v1/daemon/heartbeat")
async def post_heartbeat(request: Request):
    return await handle(request, heartbeat)


@app.post("/api/v1/daemon/poll-commands")
async def post_poll(request: Request):
    return await handle(request, poll_commands)


@app.post("/api/v1/daemon/ack")
async def post_ack(request: Request):
    return await handle(request, ack_command)


@app.post("/api/v1/daemon/submit-summary")
async def post_summary(request: Request):
    return await handle(request, submit_summary, 256 * 1024)


@app.get("/livez")
async def livez():
    return {"status": "ok"}


@app.get("/readyz")
async def readyz(request: Request):
    ready = await request.app.state.registry.ready()
    return JSONResponse({"status": "ok" if ready else "unavailable"}, status_code=200 if ready else 503)


@app.get("/metrics")
async def metrics():
    return Response(generate_latest(), headers={"Content-Type": CONTENT_TYPE_LATEST})
