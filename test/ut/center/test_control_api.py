"""Control API and persistence decisions, without a running DB or HTTP server."""

import json
import sys
import tempfile
import unittest
from contextlib import asynccontextmanager
from pathlib import Path
from string import Template
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock, patch

CONTROL = Path(__file__).resolve().parents[3] / "deploy/center/control"
with patch.object(sys, "path", [str(CONTROL), *sys.path]):
    import app
    from registry import Registry, RequestError


class ControlTest(unittest.IsolatedAsyncioTestCase):
    def test_standalone_proxy_and_image_ports(self):
        # fcntl is used only when the Linux service acquires the data-volume lock.
        with patch.dict(sys.modules, {"fcntl": MagicMock()} if sys.platform == "win32" else {}):
            with patch.object(sys, "path", [str(CONTROL), *sys.path]):
                import center
        proxy = Template((CONTROL / "standalone-haproxy.cfg").read_text()).substitute(
            center.ADDRESSES, public_port=center.PUBLIC_PORT
        )
        self.assertIn(f"bind :{center.PUBLIC_PORT} ssl", proxy)
        self.assertIn(f'server i8 {center.ADDRESSES["control"]}', proxy)
        self.assertIn(f'server vmauth {center.ADDRESSES["auth"]}', proxy)
        dockerfile = (CONTROL / "Dockerfile").read_text()
        self.assertIn(f"EXPOSE {center.PUBLIC_PORT}\n", dockerfile)
        commands = [json.loads(line[4:]) for line in dockerfile.splitlines() if line.startswith("CMD ")]
        self.assertTrue(commands)
        for command in commands:
            self.assertEqual(command[command.index("--port") + 1], center.ADDRESSES["control"].split(":")[1])

    def test_token_rotation_preserves_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            tokens = Path(directory) / "tokens.json"
            tokens.write_text(json.dumps({"job": ["a" * 32, "b" * 32]}))
            credentials = app.load_credentials(tokens)
            for token in ("a" * 32, "b" * 32):
                headers = MagicMock()
                headers.getlist.return_value = ["Bearer " + token]
                request = SimpleNamespace(
                    headers=headers, app=SimpleNamespace(state=SimpleNamespace(credentials=credentials))
                )
                self.assertEqual(app.authorized_scope(request), "job")

    async def test_streamed_body_limits(self):
        for limit in (app.MAX_BODY, 256 * 1024):
            for extra in (0, 1):

                async def chunks(size=limit + extra):
                    yield b"{}"
                    yield b" " * (size - 2)

                request = SimpleNamespace(headers={"content-type": "application/json"}, stream=chunks)
                if extra:
                    with self.assertRaises(RequestError) as caught:
                        await app.read_json(request, limit)
                    self.assertEqual(caught.exception.status, 413)
                else:
                    self.assertEqual(await app.read_json(request, limit), {})

    async def test_invalid_heartbeat_does_not_write(self):
        registry = AsyncMock()
        payload = {"session_id": "s", "status_seq": 1, "collect_state": "running"}
        for invalid in ({"status_seq": True}, {"metadata": {"cpu_percent": 101}}):
            with self.assertRaises(RequestError) as caught:
                await app.heartbeat(registry, "scope", {**payload, **invalid})
            self.assertEqual(caught.exception.status, 400)
        registry.heartbeat.assert_not_awaited()
        self.assertEqual(await app.heartbeat(registry, "scope", payload), {"accepted": True})
        registry.heartbeat.assert_awaited_once_with("scope", "s", 1, "running", None)

    async def test_register_poll_and_ack(self):
        registry = AsyncMock()
        registry.register.return_value = "s"
        reply = await app.register(registry, "job", {"instance_id": "boot", "daemon_id": "node"})
        self.assertEqual(reply["session_id"], "s")
        registry.command_exchange.return_value = None
        self.assertEqual(await app.poll_commands(registry, "job", {"session_id": "s", "wait_s": 0}), {"command": None})
        result = {"rank": 0, "status": "success"}
        registry.command_exchange.return_value = {"accepted": True}
        await app.ack_command(
            registry, "job", {"session_id": "s", "request_id": "stop", "version": 1, "result": result}
        )
        registry.command_exchange.assert_awaited_with("job", "s", ("stop", 1, result))

    async def test_summary_retry_and_conflict(self):
        conn = AsyncMock()

        @asynccontextmanager
        async def connection():
            yield conn

        with patch.object(Registry, "_connection", staticmethod(connection)):
            registry = Registry("dbname=unused")
            for matches in (True, False):
                active, inserted, duplicate = AsyncMock(), MagicMock(rowcount=0), AsyncMock()
                active.fetchone.return_value = ("node", True)
                duplicate.fetchone.return_value = (matches,)
                conn.execute.side_effect = [active, inserted, duplicate]
                payload = {"session_id": "s", "batch_id": "b", "schema_version": 1, "records": [{"v": 1}]}
                if matches:
                    self.assertEqual(
                        await app.submit_summary(registry, "job", payload),
                        {"persisted": True, "batch_id": "b", "count": 1},
                    )
                else:
                    with self.assertRaises(RequestError) as caught:
                        await app.submit_summary(registry, "job", payload)
                    self.assertEqual(caught.exception.status, 409)
                args = conn.execute.await_args.args[1]
                self.assertEqual(args, (1, json.dumps([{"v": 1}], separators=(",", ":")).encode(), "job", "node", "b"))
