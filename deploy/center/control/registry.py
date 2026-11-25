"""Shared daemon session state in PostgreSQL."""

import argparse
import asyncio
import math
import os
import json
import re
import secrets
from contextlib import asynccontextmanager
from pathlib import Path

import psycopg
from psycopg import sql
from psycopg.types.json import Jsonb
from psycopg_pool import AsyncConnectionPool


class RequestError(Exception):
    def __init__(self, status: int, code: str):
        super().__init__(code)
        self.status = status
        self.code = code


def read_dsn() -> str:
    path = os.environ.get("I8_DATABASE_URL_FILE")
    if path:
        dsn = Path(path).read_text(encoding="utf-8").strip()
    else:
        dsn = os.environ.get("I8_DATABASE_URL", "").strip()
    if not dsn:
        raise ValueError("Set I8_DATABASE_URL_FILE or I8_DATABASE_URL")
    return dsn


async def migrate(dsn: str) -> None:
    async with await psycopg.AsyncConnection.connect(dsn, connect_timeout=5) as conn:
        await conn.execute("SET LOCAL statement_timeout = '30s'")
        await conn.execute("SELECT pg_advisory_xact_lock(hashtextextended('msmonitor_i8 migration', 0))")
        await conn.execute("CREATE SCHEMA IF NOT EXISTS msmonitor_i8")
        await conn.execute("""
            CREATE TABLE IF NOT EXISTS msmonitor_i8.summary_batches (
                scope TEXT NOT NULL, daemon_id TEXT NOT NULL, batch_id TEXT NOT NULL,
                schema_version INTEGER NOT NULL, records BYTEA NOT NULL,
                received_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
                PRIMARY KEY (scope, daemon_id, batch_id)
            )
        """)
        await conn.execute("""
            CREATE TABLE IF NOT EXISTS msmonitor_i8.commands (
                scope TEXT NOT NULL, daemon_id TEXT NOT NULL,
                request_id TEXT NOT NULL, version BIGINT NOT NULL CHECK (version > 0),
                command JSONB NOT NULL, result JSONB, delivered_to TEXT,
                created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
                PRIMARY KEY (scope, daemon_id, request_id, version)
            )
        """)
        await conn.execute("""
            CREATE INDEX IF NOT EXISTS pending_commands
            ON msmonitor_i8.commands (scope, daemon_id, created_at, request_id, version)
            WHERE result IS NULL
        """)
        await conn.execute("""
            CREATE TABLE IF NOT EXISTS msmonitor_i8.daemon_sessions (
                session_id TEXT PRIMARY KEY,
                scope TEXT NOT NULL,
                daemon_id TEXT NOT NULL,
                instance_id TEXT NOT NULL,
                active BOOLEAN NOT NULL DEFAULT TRUE,
                status_seq BIGINT NOT NULL DEFAULT -1,
                collect_state TEXT,
                last_seen TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
                UNIQUE (scope, daemon_id, instance_id)
            )
        """)
        await conn.execute("""
            ALTER TABLE msmonitor_i8.daemon_sessions
                ADD COLUMN IF NOT EXISTS registration_metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                ADD COLUMN IF NOT EXISTS heartbeat_metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                ADD COLUMN IF NOT EXISTS heartbeat_metadata_seq BIGINT
        """)
        await conn.execute("""
            CREATE UNIQUE INDEX IF NOT EXISTS daemon_current_session
                ON msmonitor_i8.daemon_sessions (scope, daemon_id) WHERE active
        """)
        await conn.execute("""
            CREATE OR REPLACE VIEW msmonitor_i8.current_daemons AS
                SELECT scope, daemon_id, instance_id, status_seq, collect_state, last_seen,
                       last_seen > statement_timestamp() - INTERVAL '60 seconds' AS online
                FROM msmonitor_i8.daemon_sessions
                WHERE active
        """)
        await conn.execute("""
            CREATE OR REPLACE VIEW msmonitor_i8.daemon_details AS
                SELECT scope, daemon_id, instance_id, status_seq,
                       registration_metadata, heartbeat_metadata, heartbeat_metadata_seq
                FROM msmonitor_i8.daemon_sessions
                WHERE active
        """)


async def runtime_writable(conn):
    row = await conn.execute("""
        SELECT current_setting('transaction_read_only') = 'off'
          AND has_schema_privilege('msmonitor_i8', 'USAGE')
          AND bool_and(has_table_privilege(table_name, privilege))
        FROM (VALUES
            ('msmonitor_i8.daemon_sessions', 'SELECT'), ('msmonitor_i8.daemon_sessions', 'INSERT'),
            ('msmonitor_i8.daemon_sessions', 'UPDATE'), ('msmonitor_i8.commands', 'SELECT'),
            ('msmonitor_i8.commands', 'UPDATE'), ('msmonitor_i8.summary_batches', 'SELECT'),
            ('msmonitor_i8.summary_batches', 'INSERT')
        ) AS required(table_name, privilege)
    """)
    return (await row.fetchone())[0]


async def authorize_runtime(migration_dsn, runtime_file):
    runtime_dsn = Path(runtime_file).read_text(encoding="utf-8").strip()
    async with await psycopg.AsyncConnection.connect(runtime_dsn, autocommit=True, connect_timeout=5) as runtime:
        await runtime.execute("SET client_encoding TO UTF8")
        identity = await runtime.execute("SELECT current_user")
        role = sql.Identifier((await identity.fetchone())[0])
        async with await psycopg.AsyncConnection.connect(migration_dsn, connect_timeout=5) as owner:
            for grant in (
                "GRANT USAGE ON SCHEMA msmonitor_i8 TO {}",
                "GRANT SELECT, INSERT, UPDATE ON msmonitor_i8.daemon_sessions TO {}",
                "GRANT SELECT, UPDATE ON msmonitor_i8.commands TO {}",
                "GRANT SELECT, INSERT ON msmonitor_i8.summary_batches TO {}",
            ):
                await owner.execute(sql.SQL(grant).format(role))
        if not await runtime_writable(runtime):
            raise ValueError("Runtime role lacks required database privileges")


async def _configure_connection(conn: psycopg.AsyncConnection) -> None:
    # A registration reads session state in a fresh snapshot after acquiring its lock.
    await conn.set_isolation_level(psycopg.IsolationLevel.READ_COMMITTED)


class Registry:
    # One event loop per registry. Coalesce concurrent idle poll checks only.
    POLL_BATCH_SIZE = 128
    POLL_BATCH_DELAY = 0.005

    def __init__(
        self,
        dsn: str,
        *,
        max_size: int = 8,
        timeout: float = 1.0,
        max_waiting: int = 128,
        statement_timeout_ms: int = 2000,
    ):
        self._poll_keys = []
        self._poll_task = None
        self._pool = AsyncConnectionPool(
            dsn,
            open=False,
            min_size=1,
            max_size=max_size,
            timeout=timeout,
            max_waiting=max_waiting,
            configure=_configure_connection,
            kwargs={
                "connect_timeout": max(2, math.ceil(timeout)),
                "options": f"-c statement_timeout={statement_timeout_ms}",
            },
        )

    async def open(self) -> None:
        await self._pool.open()

    async def close(self) -> None:
        await self._pool.close()

    @asynccontextmanager
    async def _connection(self):
        try:
            # The pool context commits on success and rolls back on exceptions.
            async with self._pool.connection() as conn:
                yield conn
        except psycopg.Error as error:
            raise RequestError(503, "STATE_UNAVAILABLE") from error

    async def ready(self) -> bool:
        try:
            async with self._connection() as conn:
                cursor = await conn.execute("""
                    SELECT current_setting('transaction_read_only') = 'off',
                           EXISTS (
                               SELECT registration_metadata, heartbeat_metadata, heartbeat_metadata_seq
                               FROM msmonitor_i8.daemon_sessions LIMIT 1
                           )
                """)
                writable, _ = await cursor.fetchone()
                await conn.execute("SELECT result, delivered_to FROM msmonitor_i8.commands LIMIT 0")
                await conn.execute("SELECT records FROM msmonitor_i8.summary_batches LIMIT 0")
                return writable and await runtime_writable(conn)
        except RequestError:
            return False

    async def submit_summary(self, scope, session, batch, version, records):
        async with self._connection() as conn:
            daemon = await self._active_daemon(conn, scope, session)
            key = (scope, daemon, batch)
            cursor = await conn.execute(
                """
                INSERT INTO msmonitor_i8.summary_batches (scope, daemon_id, batch_id, schema_version, records)
                VALUES (%s, %s, %s, %s, %s) ON CONFLICT DO NOTHING
            """,
                (*key, version, records),
            )
            if not cursor.rowcount:
                cursor = await conn.execute(
                    """
                    SELECT schema_version = %s AND records = %s FROM msmonitor_i8.summary_batches
                    WHERE scope = %s AND daemon_id = %s AND batch_id = %s
                """,
                    (version, records, *key),
                )
                if not (await cursor.fetchone())[0]:
                    raise RequestError(409, "BATCH_CONFLICT")

    async def _active_daemon(self, conn, scope, session):
        cursor = await conn.execute(
            """
            SELECT daemon_id, active FROM msmonitor_i8.daemon_sessions
            WHERE scope = %s AND session_id = %s FOR SHARE
        """,
            (scope, session),
        )
        identity = await cursor.fetchone()
        if not identity:
            raise RequestError(403, "UNKNOWN_SESSION")
        if not identity[1]:
            raise RequestError(409, "INSTANCE_REPLACED")
        return identity[0]

    async def _read_poll_batch(self, keys):
        await asyncio.sleep(self.POLL_BATCH_DELAY)
        if self._poll_keys is keys:
            self._poll_keys = []
            self._poll_task = None
        scopes, sessions = zip(*dict.fromkeys(keys))
        async with self._connection() as conn:
            cursor = await conn.execute(
                """
                SELECT s.scope, s.session_id, s.active, EXISTS (
                    SELECT 1 FROM msmonitor_i8.commands c
                    WHERE c.scope = s.scope AND c.daemon_id = s.daemon_id AND c.result IS NULL
                )
                FROM unnest(%s::text[], %s::text[]) AS wanted(scope, session_id)
                JOIN msmonitor_i8.daemon_sessions s USING (scope, session_id)
            """,
                (list(scopes), list(sessions)),
            )
            return {(scope, session): (active, pending) for scope, session, active, pending in await cursor.fetchall()}

    async def _has_pending_command(self, scope, session):
        if self._poll_task is None or len(self._poll_keys) >= self.POLL_BATCH_SIZE:
            self._poll_keys = []
            self._poll_task = asyncio.create_task(self._read_poll_batch(self._poll_keys))
            # A disconnected request must not cancel a batch shared by other callers.
            self._poll_task.add_done_callback(lambda t: None if t.cancelled() else t.exception())
        self._poll_keys.append((scope, session))
        rows = await asyncio.shield(self._poll_task)
        identity = rows.get((scope, session))
        if identity is None:
            raise RequestError(403, "UNKNOWN_SESSION")
        active, pending = identity
        if not active:
            raise RequestError(409, "INSTANCE_REPLACED")
        return pending

    async def command_exchange(self, scope, session, acknowledgement=None):
        if acknowledgement is None and not await self._has_pending_command(scope, session):
            return None
        # Revalidate the session and command under lock before delivering or acknowledging.
        async with self._connection() as conn:
            daemon = await self._active_daemon(conn, scope, session)
            if acknowledgement is None:
                cursor = await conn.execute(
                    """
                    SELECT request_id, version, command, delivered_to FROM msmonitor_i8.commands AS pending
                    WHERE scope = %s AND daemon_id = %s AND result IS NULL
                    ORDER BY (command->>'kind' = 'stop' AND EXISTS (
                        SELECT 1 FROM msmonitor_i8.commands AS active
                        WHERE active.scope = pending.scope AND active.daemon_id = pending.daemon_id
                          AND active.command->'rank' = pending.command->'rank'
                          AND active.command->>'kind' = 'collect'
                          AND active.delivered_to IS NOT NULL AND active.result IS NULL
                    )) DESC, created_at, request_id, version LIMIT 1 FOR UPDATE
                """,
                    (scope, daemon),
                )
                row = await cursor.fetchone()
                if row is None:
                    return None
                request_id, version, command, delivered_to = row
                if delivered_to != session:
                    await conn.execute(
                        """
                        UPDATE msmonitor_i8.commands SET delivered_to = %s
                        WHERE scope = %s AND daemon_id = %s AND request_id = %s AND version = %s
                    """,
                        (session, scope, daemon, request_id, version),
                    )
                return {"request_id": request_id, "version": version, **command}

            request_id, version, result = acknowledgement
            key = (scope, daemon, request_id, version)
            cursor = await conn.execute(
                """
                SELECT command, delivered_to, result FROM msmonitor_i8.commands
                WHERE scope = %s AND daemon_id = %s AND request_id = %s AND version = %s FOR UPDATE
            """,
                key,
            )
            row = await cursor.fetchone()
            if row is None:
                raise RequestError(404, "UNKNOWN_COMMAND")
            command, owner, previous = row
            if owner != session or command["rank"] != result["rank"]:
                raise RequestError(409, "COMMAND_MISMATCH")
            if command["kind"] == "export" and result["status"] == "success":
                if not result.get("uri") or not result.get("sha256"):
                    raise RequestError(400, "MISSING_EVIDENCE")
            if previous is None:
                await conn.execute(
                    """
                    UPDATE msmonitor_i8.commands SET result = %s
                    WHERE scope = %s AND daemon_id = %s AND request_id = %s AND version = %s
                """,
                    (Jsonb(result), *key),
                )
            elif previous != result:
                raise RequestError(409, "RESULT_CONFLICT")
            return {"accepted": True}

    def pool_stats(self) -> dict[str, int]:
        return self._pool.get_stats()

    async def register(self, scope: str, daemon_id: str, instance_id: str, metadata: dict | None = None) -> str:
        async with self._connection() as conn:
            # Serialize instance changes even before this daemon has any session rows.
            await conn.execute(
                "SELECT pg_advisory_xact_lock(hashtextextended(%s, 0))",
                (f"msmonitor_i8:{scope}:{daemon_id}",),
            )
            cursor = await conn.execute(
                """
                SELECT session_id, active FROM msmonitor_i8.daemon_sessions
                WHERE scope = %s AND daemon_id = %s AND instance_id = %s
            """,
                (scope, daemon_id, instance_id),
            )
            existing = await cursor.fetchone()
            if existing:
                session_id, active = existing
                if not active:
                    raise RequestError(409, "INSTANCE_REPLACED")
                return session_id

            await conn.execute(
                """
                UPDATE msmonitor_i8.daemon_sessions SET active = FALSE
                WHERE scope = %s AND daemon_id = %s AND active
            """,
                (scope, daemon_id),
            )
            session_id = secrets.token_hex(24)
            await conn.execute(
                """
                INSERT INTO msmonitor_i8.daemon_sessions
                    (session_id, scope, daemon_id, instance_id, registration_metadata)
                VALUES (%s, %s, %s, %s, %s)
            """,
                (session_id, scope, daemon_id, instance_id, Jsonb(metadata or {})),
            )
            return session_id

    async def heartbeat(self, scope: str, session_id: str, seq: int, state: str, metadata: dict | None = None) -> None:
        metadata_value = Jsonb(metadata) if metadata is not None else None
        metadata_seq = seq if metadata is not None else None
        async with self._connection() as conn:
            # PostgreSQL rechecks the predicates after any concurrent row update.
            updated = await conn.execute(
                """
                UPDATE msmonitor_i8.daemon_sessions
                SET status_seq = %s, collect_state = %s,
                    last_seen = greatest(last_seen, clock_timestamp()),
                    heartbeat_metadata = COALESCE(%s, heartbeat_metadata),
                    heartbeat_metadata_seq = COALESCE(%s, heartbeat_metadata_seq)
                WHERE session_id = %s AND scope = %s AND active AND status_seq < %s
            """,
                (seq, state, metadata_value, metadata_seq, session_id, scope, seq),
            )
            if updated.rowcount:
                return

            cursor = await conn.execute(
                """
                SELECT active FROM msmonitor_i8.daemon_sessions
                WHERE session_id = %s AND scope = %s
            """,
                (session_id, scope),
            )
            session = await cursor.fetchone()
            if session is None:
                raise RequestError(403, "UNKNOWN_SESSION")
            if not session[0]:
                raise RequestError(409, "INSTANCE_REPLACED")


async def operator_command(args):
    key = (args.scope, args.daemon_id, args.request_id, args.version)
    async with await psycopg.AsyncConnection.connect(read_dsn(), connect_timeout=5) as conn:
        if args.action == "enqueue":
            if not 0 <= args.rank < 2**63:
                raise ValueError("Invalid rank")
            command = {"kind": args.kind, "rank": args.rank}
            if args.kind == "collect":
                if args.duration_s is None or not 1 <= args.duration_s < 2**63:
                    raise ValueError("Collection requires a positive duration")
                command["duration_s"] = args.duration_s
            elif args.duration_s is not None:
                raise ValueError("Only collection accepts a duration")
            if args.kind == "export":
                if args.start_ns is None or args.end_ns is None or not 0 <= args.start_ns < args.end_ns < 2**63:
                    raise ValueError("Export requires a valid [start-ns, end-ns) interval")
                command.update(start_ns=args.start_ns, end_ns=args.end_ns)
            elif args.start_ns is not None or args.end_ns is not None:
                raise ValueError("Only export accepts a time range")
            await conn.execute(
                """
                INSERT INTO msmonitor_i8.commands (scope, daemon_id, request_id, version, command)
                VALUES (%s, %s, %s, %s, %s) ON CONFLICT DO NOTHING
            """,
                (*key, Jsonb(command)),
            )
        cursor = await conn.execute(
            """
            SELECT command, delivered_to IS NOT NULL, result FROM msmonitor_i8.commands
            WHERE scope = %s AND daemon_id = %s AND request_id = %s AND version = %s
        """,
            key,
        )
        row = await cursor.fetchone()
        if row is None:
            raise ValueError("Unknown command")
        saved, delivered, result = row
        if args.action == "enqueue" and command != saved:
            raise ValueError("Existing request ID and version have different parameters")
    print(json.dumps({"command": saved, "delivered": delivered, "result": result}))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    migration = actions.add_parser("migrate")
    migration.add_argument("--runtime-dsn-file", help="Grant and verify application access after migration")
    enqueue = actions.add_parser("enqueue")
    show = actions.add_parser("show")
    for action in (enqueue, show):
        for field in ("scope", "daemon-id", "request-id"):
            action.add_argument("--" + field, required=True)
        action.add_argument("--version", type=int, default=1)
    enqueue.add_argument("--kind", choices=("collect", "stop", "export"), required=True)
    enqueue.add_argument("--rank", type=int, required=True)
    enqueue.add_argument("--duration-s", type=int)
    enqueue.add_argument("--start-ns", type=int)
    enqueue.add_argument("--end-ns", type=int)
    args = parser.parse_args()
    if args.action != "migrate":
        for value in (args.scope, args.daemon_id, args.request_id):
            if not re.fullmatch(r"[a-zA-Z0-9_.-]{1,128}", value):
                parser.error("Invalid command identity")
        if not 1 <= args.version < 2**63:
            parser.error("Invalid version")
        try:
            asyncio.run(operator_command(args))
        except (ValueError, OSError, psycopg.Error) as error:
            parser.exit(1, (str(error) if isinstance(error, ValueError) else "Command database unavailable") + "\n")
        return
    try:
        dsn = read_dsn()
        asyncio.run(migrate(dsn))
        if args.runtime_dsn_file:
            asyncio.run(authorize_runtime(dsn, args.runtime_dsn_file))
    except (OSError, ValueError, psycopg.Error):
        parser.exit(
            1, "I8 schema migration failed. Check migration ownership, grant rights and runtime database access.\n"
        )


if __name__ == "__main__":
    main()
