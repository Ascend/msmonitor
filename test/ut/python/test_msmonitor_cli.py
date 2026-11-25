# -------------------------------------------------------------------------
# This file is part of the MindStudio project.
# Copyright (c) 2026 Huawei Technologies Co.,Ltd.
#
# MindStudio is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -------------------------------------------------------------------------

"""msmonitor_cli 单测：参数映射、reply 报文组织、collector 内存状态、控制通道与
数据处理子进程生命周期。

运行：python -m unittest discover -s test/ut/python -p "test_msmonitor_cli.py"
"""

import io
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

_PLUGIN_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "plugin"))
sys.path.insert(0, _PLUGIN_DIR)

from msmonitor_cli import commands  # noqa: E402
from msmonitor_cli import main  # noqa: E402
from msmonitor_cli import collector  # noqa: E402
from msmonitor_cli import config  # noqa: E402
from msmonitor_cli import processor  # noqa: E402
from msmonitor_cli.config import ConfigError, MonitorConfig  # noqa: E402


# PTA 侧 dyno_str_to_json 的等价实现：逐行 split("=")，只收两段
def _dyno_parse(payload: str) -> dict:
    out = {}
    for pair in payload.split("\n"):
        parts = pair.split("=")
        if len(parts) == 2:
            out[parts[0]] = parts[1].strip()
    return out


def _args(**kwargs):
    base = dict(
        job_id=None,
        output_dir=tempfile.gettempdir(),
        mspti_activity_kind=None,
        report_interval_s=None,
        json_rotate_log_lines=None,
        json_rotate_log_files=None,
        server_host=None,
        server_port=None,
        remote_write_url=None,
        token_file=None,
        token=None,
        ca_file=None,
        monitor_target="trace",
    )
    base.update(kwargs)
    return SimpleNamespace(**base)


class ReplyFormatTest(unittest.TestCase):
    """reply 必须能被训练侧按 KEY=VALUE 解析并进入 monitor 分支。"""

    def _reply(self, **kwargs):
        cfg = commands.load_config(_args(**kwargs))
        commands._validate(cfg)
        return commands.build_reply(cfg)

    def test_reply_should_contain_required_keys_when_built(self):
        parsed = _dyno_parse(self._reply())
        for key in (
            "MSPTI_ACTIVITY_KIND",
            "REPORT_INTERVAL_S",
            "DURATION",
            "NPU_MONITOR_START",
            "NPU_MONITOR_STOP",
            "NPU_MONITOR_LOG_FILE",
            "NPU_MONITOR_EXPORT_TYPE",
        ):
            self.assertIn(key, parsed)
        self.assertEqual(parsed["is_valid"], "true")
        self.assertEqual(parsed["NPU_MONITOR_START"], "true")
        self.assertEqual(parsed["NPU_MONITOR_STOP"], "false")

    def test_reply_should_not_end_with_newline_when_built(self):
        # 尾部换行会产生一个空行，dyno_str_to_json 虽会跳过，但报文应与 dyno 保持一致
        self.assertFalse(self._reply().endswith("\n"))

    def test_log_file_should_equal_output_dir_when_built(self):
        with tempfile.TemporaryDirectory() as tmp:
            parsed = _dyno_parse(self._reply(output_dir=tmp))
        self.assertEqual(parsed["NPU_MONITOR_LOG_FILE"], str(Path(tmp).resolve()))

    def test_rotate_should_render_empty_value_when_not_specified(self):
        parsed = _dyno_parse(self._reply())
        self.assertEqual(parsed["JSONL_ROTATE_LOG_LINES"], "")
        self.assertEqual(parsed["JSONL_ROTATE_LOG_FILES"], "")

    def test_rotate_should_render_given_values_when_specified(self):
        parsed = _dyno_parse(self._reply(json_rotate_log_lines=5000, json_rotate_log_files=3))
        self.assertEqual(parsed["JSONL_ROTATE_LOG_LINES"], "5000")
        self.assertEqual(parsed["JSONL_ROTATE_LOG_FILES"], "3")

    def test_report_interval_should_map_to_uint32_when_given(self):
        parsed = _dyno_parse(self._reply(report_interval_s=15))
        self.assertEqual(parsed["REPORT_INTERVAL_S"], "15")

    def test_export_type_should_be_jsonl_when_built(self):
        parsed = _dyno_parse(self._reply())
        self.assertEqual(parsed["NPU_MONITOR_EXPORT_TYPE"], "Jsonl")

    def test_stop_reply_should_flip_monitor_flags_when_built(self):
        cfg = commands.load_config(_args())
        commands._validate(cfg)
        parsed = _dyno_parse(commands.build_stop_reply(cfg))
        self.assertEqual(parsed["NPU_MONITOR_START"], "false")
        self.assertEqual(parsed["NPU_MONITOR_STOP"], "true")
        # 停止配置同样要过训练侧 7 个必填键校验
        for key in (
            "MSPTI_ACTIVITY_KIND",
            "REPORT_INTERVAL_S",
            "DURATION",
            "NPU_MONITOR_LOG_FILE",
            "NPU_MONITOR_EXPORT_TYPE",
        ):
            self.assertIn(key, parsed)

    def test_every_line_should_be_parsable_when_built(self):
        lines = self._reply().split("\n")
        self.assertTrue(all(line.count("=") == 1 for line in lines), lines)


class ValidateTest(unittest.TestCase):
    def _validate(self, **kwargs):
        cfg = commands.load_config(_args(**kwargs))
        commands._validate(cfg)

    def test_validate_should_reject_memory_when_given(self):
        with self.assertRaises(ConfigError):
            self._validate(mspti_activity_kind="Memory")

    def test_validate_should_reject_non_exportable_kinds_when_given(self):
        for kind in ("Memory", "MemSet", "MemCpy", "Hccl"):
            with self.assertRaises(ConfigError):
                self._validate(mspti_activity_kind=kind)

    def test_validate_should_reject_empty_activity_when_given(self):
        with self.assertRaises(ConfigError):
            self._validate(mspti_activity_kind="")

    def test_validate_should_reject_negative_interval_when_given(self):
        self._validate(report_interval_s=0)
        with self.assertRaises(ConfigError):
            self._validate(report_interval_s=-1)

    def test_validate_should_reject_interval_above_uint32_when_given(self):
        with self.assertRaises(ConfigError):
            self._validate(report_interval_s=0x100000000)

    def test_validate_should_reject_output_dir_when_absent(self):
        with self.assertRaises(ConfigError):
            self._validate(output_dir="")

    def test_validate_should_reject_output_dir_when_contains_equals(self):
        with self.assertRaises(ConfigError):
            self._validate(output_dir=os.path.join(tempfile.gettempdir(), "a=b"))

    def test_validate_should_reject_rotate_lines_when_out_of_range(self):
        with self.assertRaises(ConfigError):
            self._validate(json_rotate_log_lines=99)

    def test_validate_should_reject_metric_without_center_when_given(self):
        with self.assertRaises(ConfigError):
            self._validate(monitor_target="metric")

    def test_validate_should_allow_metric_when_center_given(self):
        self._validate(
            monitor_target="metric",
            server_host="10.0.0.1",
            server_port=8443,
            remote_write_url="https://center/api/write",
            token_file="/t",
            ca_file="/ca",
        )

    def test_validate_should_allow_metric_when_plain_token_given(self):
        self._validate(
            monitor_target="metric",
            server_host="10.0.0.1",
            server_port=8443,
            remote_write_url="https://center/api/write",
            token="plain-token",
            ca_file="/ca",
        )

    def test_validate_should_reject_both_token_and_token_file_when_given(self):
        with self.assertRaises(ConfigError):
            self._validate(token="plain-token", token_file="/t")


class ParserTest(unittest.TestCase):
    """子命令拆分后每个入口必须绑定到正确的 handler。"""

    def test_handler_should_match_subcommand_when_parsed(self):
        parser = main.build_parser()
        self.assertIs(
            parser.parse_args(["start", "--output-dir", tempfile.gettempdir()]).handler,
            commands.cmd_start,
        )
        self.assertIs(parser.parse_args(["stop"]).handler, commands.cmd_stop)
        self.assertIs(parser.parse_args(["collect", "start"]).handler, commands.cmd_collect_start)
        self.assertIs(parser.parse_args(["collect", "stop"]).handler, commands.cmd_collect_stop)

    def test_output_dir_should_be_required_when_start(self):
        # argparse 的报错信息走 stderr，这里屏蔽掉避免污染测试输出
        with mock.patch("sys.stderr", new=io.StringIO()):
            with self.assertRaises(SystemExit):
                main.build_parser().parse_args(["start"])

    def test_collect_start_should_reject_when_not_started(self):
        # 未落盘 start 配置时，在拉起 collector 之前就应报错
        with mock.patch.object(commands, "_read_started_config", return_value=None):
            with self.assertRaises(ConfigError):
                commands.cmd_collect_start(_args())

    def test_started_config_should_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = commands.load_config(
                _args(
                    output_dir=tmp,
                    job_id="job-1",
                    monitor_target="metric",
                    server_host="10.0.0.1",
                    remote_write_url="https://c/w",
                    token_file="/t",
                    ca_file="/ca",
                )
            )
            cfg.state_dir = tmp
            commands._write_started_config(cfg)
            saved = commands._read_started_config(cfg)
        self.assertEqual(saved["output_dir"], os.path.abspath(tmp))
        self.assertEqual(saved["job_id"], "job-1")
        self.assertEqual(saved["monitor_target"], "metric")

    def test_start_should_reject_output_dir_change_when_running(self):
        # 已在运行时换 output_dir 会让 processor 扫错目录，必须在拉起前拒绝
        started = {"output_dir": "/tmp/msmonitor-old"}
        with (
            mock.patch.object(commands, "_running_pid", return_value=4321),
            mock.patch.object(commands, "_read_started_config", return_value=started),
            mock.patch.object(commands, "_ensure_dir"),
        ):
            with self.assertRaises(ConfigError):
                commands.cmd_start(_args(output_dir=tempfile.gettempdir()))

    def test_start_should_only_report_when_running(self):
        # start 只执行一次，重复执行只提示现有实例，不再拉起或补齐任何进程
        started = {"output_dir": os.path.abspath(tempfile.gettempdir())}
        with (
            mock.patch.object(commands, "_running_pid", return_value=4321),
            mock.patch.object(commands, "_read_started_config", return_value=started),
            mock.patch.object(commands, "_ensure_collector") as ensure_collector,
            mock.patch.object(commands, "_ensure_processor") as ensure_processor,
            mock.patch.object(commands, "_latest_log_path"),
            mock.patch("builtins.print"),
        ):
            code = commands.cmd_start(_args(output_dir=tempfile.gettempdir()))
        self.assertEqual(code, commands.EXIT_OK)
        ensure_collector.assert_not_called()
        ensure_processor.assert_not_called()

    def test_monitor_target_should_be_parsed_when_start(self):
        args = main.build_parser().parse_args(
            [
                "start",
                "--output-dir",
                tempfile.gettempdir(),
                "--monitor-target",
                "metric",
            ]
        )
        self.assertEqual(args.monitor_target, "metric")

    def test_monitor_target_should_default_to_trace_and_metric_when_absent(self):
        self.assertEqual(MonitorConfig().monitor_target, ["trace", "metric"])


class LogDirTest(unittest.TestCase):
    """日志目录默认落在产物目录下，可被 MSMONITOR_LOG_PATH 覆盖。"""

    def test_log_dir_should_default_to_output_dir_when_unset(self):
        cfg = MonitorConfig(output_dir=os.path.join(tempfile.gettempdir(), "msmonitor-out"))
        with mock.patch.dict(os.environ, {"MSMONITOR_LOG_PATH": ""}):
            self.assertEqual(commands._log_dir(cfg), Path(cfg.output_dir) / "log")

    def test_log_dir_should_follow_env_when_set(self):
        cfg = MonitorConfig(output_dir=os.path.join(tempfile.gettempdir(), "msmonitor-out"))
        target = os.path.join(tempfile.gettempdir(), "msmonitor-log")
        with mock.patch.dict(os.environ, {"MSMONITOR_LOG_PATH": target}):
            self.assertEqual(commands._log_dir(cfg), Path(target))


class ProcessorTest(unittest.TestCase):
    """数据处理子进程：只透传 start 参数，不自行决定产物目录。"""

    def _cfg(self, **kwargs):
        cfg = commands.load_config(_args(output_dir=tempfile.gettempdir(), **kwargs))
        cfg.state_dir = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(cfg.state_dir, ignore_errors=True))
        return cfg

    def test_argv_should_reuse_python_process_when_built(self):
        argv = commands._processor_argv(self._cfg())
        self.assertEqual(argv[:3], [sys.executable, "-m", "msmonitor_cli.processor"])

    def test_argv_should_pass_start_params_when_built(self):
        cfg = self._cfg(
            job_id="job-9",
            server_host="10.0.0.1",
            server_port=9000,
            remote_write_url="https://c/w",
            token_file="/t",
            ca_file="/ca",
            monitor_target="metric",
        )
        argv = commands._processor_argv(cfg)
        for expected in (
            "--output-dir",
            "--job-id",
            "job-9",
            "10.0.0.1",
            "9000",
            "https://c/w",
            "/t",
            "/ca",
            "--monitor-target",
            "metric",
        ):
            self.assertIn(expected, argv)

    def test_argv_should_not_decide_result_dir_when_built(self):
        self.assertNotIn("--result-dir", commands._processor_argv(self._cfg()))

    def test_argv_should_pass_default_monitor_target_when_built(self):
        argv = commands._processor_argv(self._cfg())
        self.assertEqual(argv[argv.index("--monitor-target") + 1], "trace")

    def test_stop_processor_should_return_none_without_pid(self):
        self.assertIsNone(commands._stop_processor(self._cfg()))


class ProcessorMainTest(unittest.TestCase):
    """数据处理子进程入口：参数到扩展 dict 的映射与 start/stop 生命周期。"""

    @staticmethod
    def _parse(argv):
        return processor._build_parser().parse_args(argv)

    def test_output_dir_should_be_required_when_parsed(self):
        with mock.patch("sys.stderr", new_callable=io.StringIO):
            with self.assertRaises(SystemExit):
                self._parse([])

    def test_config_should_map_output_dir_to_input_dir_when_built(self):
        doc = processor._config(self._parse(["--output-dir", "/data/out"]))
        self.assertEqual(doc["input_dir"], "/data/out")
        self.assertEqual(
            set(doc),
            {
                "input_dir",
                "job_id",
                "enable_trace",
                "enable_metric",
                "trace_steps_per_file",
                "trace_keep_files",
                "server_host",
                "server_port",
                "remote_write_url",
                "token_file",
                "token",
                "ca_file",
            },
        )

    def test_config_should_use_monitorconfig_defaults_when_absent(self):
        defaults = MonitorConfig()
        doc = processor._config(self._parse(["--output-dir", "/d"]))
        self.assertEqual(doc["trace_steps_per_file"], defaults.trace_steps_per_file)
        self.assertEqual(doc["trace_keep_files"], defaults.trace_keep_files)
        self.assertEqual(doc["server_port"], defaults.server_port)
        self.assertTrue(doc["enable_trace"])
        self.assertTrue(doc["enable_metric"])

    def _run_main(self, fake_ipc, argv=None):
        fake_pkg = SimpleNamespace(ipcmonitor_C_module=fake_ipc)
        with mock.patch.dict(sys.modules, {"IPCMonitor": fake_pkg}):
            with (
                mock.patch("sys.stdout", new_callable=io.StringIO),
                mock.patch("sys.stderr", new_callable=io.StringIO),
            ):
                return processor.main(argv or ["--output-dir", "/d"])

    def test_main_should_fail_when_extension_lacks_process_api(self):
        self.assertEqual(self._run_main(SimpleNamespace()), 1)

    def test_main_should_fail_when_start_rejected(self):
        ipc = SimpleNamespace(process_start=lambda _cfg: False, process_stop=lambda: None)
        self.assertEqual(self._run_main(ipc), 1)

    def test_main_should_fail_when_start_raises(self):
        def _boom(_cfg):
            raise RuntimeError("boom")

        ipc = SimpleNamespace(process_start=_boom, process_stop=lambda: None)
        self.assertEqual(self._run_main(ipc), 1)

    def test_main_should_start_then_stop_when_signaled(self):
        called = {}
        handlers = {}

        def fake_start(cfg):
            called.update(cfg)
            return True

        def fake_stop():
            called["stopped"] = True

        def fake_signal(signum, handler):
            handlers[signum] = handler

        def fake_sleep(_seconds):
            # 主循环第一次 sleep 时触发 SIGTERM，验证退出后一定调用 process_stop
            handlers[signal.SIGTERM](signal.SIGTERM, None)

        ipc = SimpleNamespace(process_start=fake_start, process_stop=fake_stop)
        with (
            mock.patch.object(config.signal, "signal", side_effect=fake_signal),
            mock.patch.object(processor.time, "sleep", side_effect=fake_sleep),
        ):
            self.assertEqual(self._run_main(ipc, ["--output-dir", "/data/x"]), 0)
        self.assertEqual(called["input_dir"], "/data/x")
        self.assertTrue(called["stopped"])


class TerminateTest(unittest.TestCase):
    """子进程回收：SIGTERM 后必须真正退出。"""

    @unittest.skipUnless(os.name == "posix", "requires POSIX signals")
    def test_terminate_should_reap_child_when_running(self):
        proc = subprocess.Popen(  # pylint: disable=consider-using-with
            [sys.executable, "-c", "import time; time.sleep(30)"]
        )
        self.addCleanup(lambda: proc.poll() is None and proc.kill())
        commands._terminate(proc.pid, timeout_s=5.0)
        proc.wait(timeout=5)
        self.assertIsNotNone(proc.returncode)


class CollectorTest(unittest.TestCase):
    def test_apply_ctrl_should_store_reply_when_started(self):
        state = {"reply": None, "stop_reply": None}
        ok = collector.apply_ctrl(
            state,
            {
                "enabled": True,
                "reply": "is_valid=true",
                "stop_reply": "is_valid=true\nNPU_MONITOR_STOP=true",
            },
        )
        self.assertTrue(ok)
        self.assertEqual(state["reply"], "is_valid=true")
        self.assertEqual(state["stop_reply"], "is_valid=true\nNPU_MONITOR_STOP=true")

    def test_apply_ctrl_should_switch_to_stop_reply_when_stopped(self):
        state = {"reply": "start", "stop_reply": "stop"}
        ok = collector.apply_ctrl(state, {"enabled": False})
        self.assertTrue(ok)
        self.assertEqual(state["reply"], "stop")

    def test_apply_ctrl_should_reject_start_without_reply(self):
        state = {"reply": None, "stop_reply": None}
        self.assertFalse(collector.apply_ctrl(state, {"enabled": True}))

    def test_apply_ctrl_should_clear_delivered_when_started(self):
        state = {"reply": None, "stop_reply": None, "delivered": {b"rank0"}}
        collector.apply_ctrl(state, {"enabled": True, "reply": "is_valid=true"})
        self.assertEqual(state["delivered"], set())

    def test_apply_ctrl_should_clear_delivered_when_stopped(self):
        state = {"reply": "start", "stop_reply": "stop", "delivered": {b"rank0"}}
        collector.apply_ctrl(state, {"enabled": False})
        self.assertEqual(state["delivered"], set())

    def test_is_module_alive_should_reject_absent_pid_when_given(self):
        self.assertFalse(commands._is_module_alive(-1, commands.COLLECTOR_MODULE))

    @unittest.skipUnless(os.path.isdir("/proc"), "requires /proc")
    def test_is_module_alive_should_reject_other_process_when_pid_reused(self):
        # 当前测试进程存活，但 cmdline 不含 collector 模块名
        self.assertFalse(commands._is_module_alive(os.getpid(), commands.COLLECTOR_MODULE))


@unittest.skipUnless(sys.platform.startswith("linux"), "requires abstract UNIX socket")
class CtrlChannelTest(unittest.TestCase):
    """_send_ctrl 的帧格式必须与 C++ Metadata 一致，并等待 collector ack。"""

    def test_send_ctrl_should_round_trip_frame_when_collector_replies(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "dynolog.sock"
            server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            server.bind(str(path))
            server.settimeout(5)
            received = {}

            def _serve():
                data, addr = server.recvfrom(commands.METADATA_SIZE + commands.MAX_IPC_MESSAGE_SIZE)
                size, msg_type = struct.unpack(commands.METADATA_FMT, data[: commands.METADATA_SIZE])
                received["type"] = msg_type.rstrip(b"\0")
                received["body"] = json.loads(data[commands.METADATA_SIZE : commands.METADATA_SIZE + size])
                server.sendto(struct.pack(commands.METADATA_FMT, 2, b"ctrl") + b"ok", addr)

            thread = threading.Thread(target=_serve)
            thread.start()
            try:
                ok, _ = commands._send_ctrl(path, {"enabled": True, "reply": "x"})
            finally:
                thread.join()
                server.close()

        self.assertTrue(ok)
        self.assertEqual(received["type"], b"ctrl")
        self.assertTrue(received["body"]["enabled"])


if __name__ == "__main__":
    unittest.main()
