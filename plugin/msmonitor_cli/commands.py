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

"""Command implementations for the msmonitor CLI.

四个入口，职责按 dynolog daemon / npumonitor 划分：

* ``start``          拉起 collector（采集控制进程）与数据处理子进程，不下发采集配置
* ``stop``           下发停止配置后关闭 collector
* ``collect start``  下发采集配置，collector 不存在时自动拉起
* ``collect stop``   下发停止配置，collector 保持常驻

collector 与训练侧 PTA 监控进程的通信由 ``msmonitor_cli.collector`` 负责。
"""

from __future__ import annotations

import json
import os
import signal
import socket
import struct
import subprocess  # nosec B404
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from .config import (
    CONFIG_FILE,
    DEFAULT_STATE_DIR,
    PROCESSOR_PID_FILE,
    READY_FILE,
    SOCKET_FILE,
    ConfigError,
    MonitorConfig,
    remove_state_files,
    resolve,
    state_file,
    validate,
    validate_collect,
    validate_collector,
    validate_output,
)

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_CONFIG = 2
EXIT_BACKEND = 3

COLLECTOR_MODULE = "msmonitor_cli.collector"
PROCESSOR_MODULE = "msmonitor_cli.processor"

# 与 C++ Metadata{size_t size; char type[32];} 保持一致
METADATA_FMT = "=Q32s"
METADATA_SIZE = struct.calcsize(METADATA_FMT)
MAX_IPC_MESSAGE_SIZE = 4096
CTRL_TIMEOUT_S = 5.0


def _cli_values(args) -> Dict[str, Any]:
    # stop 不带任何参数，其余参数用默认值兜底
    return {
        "job_id": getattr(args, "job_id", None),
        "output_dir": getattr(args, "output_dir", None),
        "activity_kind": getattr(args, "mspti_activity_kind", None),
        "report_interval_s": getattr(args, "report_interval_s", None),
        "json_rotate_log_lines": getattr(args, "json_rotate_log_lines", None),
        "json_rotate_log_files": getattr(args, "json_rotate_log_files", None),
        "trace_steps_per_file": getattr(args, "trace_steps_per_file", None),
        "trace_keep_files": getattr(args, "trace_keep_files", None),
        "remote_write_url": getattr(args, "remote_write_url", None),
        "token_file": getattr(args, "token_file", None),
        "token": getattr(args, "token", None),
        "ca_file": getattr(args, "ca_file", None),
        "monitor_target": getattr(args, "monitor_target", None),
    }


def load_config(args) -> MonitorConfig:
    cfg = resolve(_cli_values(args))
    # 单实例约束：状态目录固定，stop 无需任何参数即可定位
    cfg.state_dir = os.path.expanduser(DEFAULT_STATE_DIR)
    # 训练进程按各自的 cwd 解析该路径，必须固化成绝对路径
    cfg.output_dir = os.path.abspath(os.path.expanduser(cfg.output_dir)) if cfg.output_dir else ""
    return cfg


def _ready_path(cfg: MonitorConfig) -> Path:
    return state_file(cfg.state_dir, READY_FILE)


def _socket_path(cfg: MonitorConfig) -> Optional[Path]:
    try:
        return Path(state_file(cfg.state_dir, SOCKET_FILE).read_text(encoding="utf-8").strip())
    except OSError:
        return None


def _read_pid(path: Path) -> Optional[int]:
    try:
        return int(path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def _pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _is_module_alive(pid: int, module: str) -> bool:
    """存活且确实是目标子进程：cmdline 校验，避免 pid 被复用时误判。"""
    if not _pid_alive(pid):
        return False
    try:
        cmdline = Path("/proc/{}/cmdline".format(pid)).read_bytes()
    except OSError:
        # 无 /proc 的环境退化为存活判断
        return True
    return module.encode() in cmdline


def _running_pid(cfg: MonitorConfig) -> Optional[int]:
    """返回存活 collector 的 pid；`.ready` 在 collector 退出时删除，因此不会被陈旧记录误导。"""
    pid = _read_pid(_ready_path(cfg))
    if pid and _is_module_alive(pid, COLLECTOR_MODULE):
        return pid
    return None


def _write_started_config(cfg: MonitorConfig) -> None:
    """把 ``start`` 时的进程级参数落盘，供 ``collect start`` 复用。

    保存 output_dir / job_id、trace 切分参数、产物去向与数据中心参数；采集参数每次由 collect 自己给。
    """
    doc = {
        "output_dir": cfg.output_dir,
        "job_id": cfg.job_id,
        "monitor_target": ",".join(cfg.monitor_target),
        "trace_steps_per_file": cfg.trace_steps_per_file,
        "trace_keep_files": cfg.trace_keep_files,
        "remote_write_url": cfg.remote_write_url,
        "token_file": cfg.token_file,
        "ca_file": cfg.ca_file,
    }
    state_file(cfg.state_dir, CONFIG_FILE).write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")


def _read_started_config(cfg: MonitorConfig) -> Optional[Dict[str, Any]]:
    try:
        doc = json.loads(state_file(cfg.state_dir, CONFIG_FILE).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return doc if isinstance(doc, dict) else None


def _remove_started_config(cfg: MonitorConfig) -> None:
    remove_state_files(cfg.state_dir, (CONFIG_FILE,))


def _purge_runtime_files(cfg: MonitorConfig) -> None:
    remove_state_files(cfg.state_dir, (READY_FILE, SOCKET_FILE))


def _send_ctrl(path: Path, doc: Dict[str, Any]) -> Tuple[bool, str]:
    """直连 collector 的 UNIX socket 下发控制消息，等待 ack。

    帧格式与 PTA 侧一致：40 字节 Metadata + payload，type 固定为 ``ctrl``。
    返回 ``(是否成功, 失败原因)``：失败原因用于区分 socket 不存在、连接被拒、
    超时等不同修复方向，调用方直接拼进报错信息。
    """
    payload = json.dumps(doc, ensure_ascii=False).encode("utf-8")
    if len(payload) > MAX_IPC_MESSAGE_SIZE:
        raise ConfigError("control message exceeds {} bytes".format(MAX_IPC_MESSAGE_SIZE))
    frame = struct.pack(METADATA_FMT, len(payload), b"ctrl") + payload

    client = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        # collector 回包走 C++ SetSocketAdress，其对抽象地址的 addrlen 是
        # 4 + strlen(name)，比 POSIX 惯例多一个 '\0'。Python bind 默认按
        # 3 + strlen(name) 注册名字，长度差 1 会导致内核按名字精确匹配失败，
        # ack 永远到不了。这里显式补一个 '\0' 对齐 C++ 的 addrlen。
        client.bind(b"\0msmonitorctl" + str(os.getpid()).encode() + b"\0")
        client.settimeout(CTRL_TIMEOUT_S)
        client.sendto(frame, str(path))
        data = client.recv(METADATA_SIZE + MAX_IPC_MESSAGE_SIZE)
    except OSError as e:
        return False, "{}: {}".format(type(e).__name__, e)
    finally:
        client.close()

    if len(data) < METADATA_SIZE:
        return False, "ack shorter than {} bytes".format(METADATA_SIZE)
    size, _ = struct.unpack(METADATA_FMT, data[:METADATA_SIZE])
    if data[METADATA_SIZE : METADATA_SIZE + size] != b"ok":
        return False, "collector replied without ok"
    return True, ""


def build_reply(cfg: MonitorConfig) -> str:
    """构造回给 PTA 监控进程的 KEY=VALUE 文本（MSPTI Monitor 采集）。

    PTA 侧 ConfigContext 用 ``is_valid`` 判定配置有效、用 ``NPU_MONITOR_START``
    判定 monitor 分支；C++ InputParser 要求布尔量是 true/false 字面量、
    REPORT_INTERVAL_S 是 uint32、EXPORT_TYPE 固定为 Jsonl（本 CLI 只落盘 Jsonl）。
    """
    return "\n".join(
        [
            "is_valid=true",
            "MSPTI_ACTIVITY_KIND={}".format(",".join(cfg.activity_kind or ())),
            "REPORT_INTERVAL_S={}".format(int(cfg.report_interval_s)),
            "DURATION={}".format(cfg.duration_s or 0),
            "NPU_MONITOR_START=true",
            "NPU_MONITOR_STOP=false",
            "NPU_MONITOR_LOG_FILE={}".format(cfg.output_dir),
            "NPU_MONITOR_EXPORT_TYPE=Jsonl",
            # 与 dyno 一致：未指定时留空，让训练侧读取自己的环境变量/默认值
            "JSONL_ROTATE_LOG_LINES={}".format("" if cfg.json_rotate_log_lines is None else cfg.json_rotate_log_lines),
            "JSONL_ROTATE_LOG_FILES={}".format("" if cfg.json_rotate_log_files is None else cfg.json_rotate_log_files),
        ]
    )


def build_stop_reply(cfg: MonitorConfig) -> str:
    """构造停止采集的 KEY=VALUE 文本。

    与 start 的字段集保持一致：C++ InputParser 要求 7 个必需键齐全，
    缺任何一个都会被整条丢弃，stop 就静默不生效。
    """
    return (
        build_reply(cfg)
        .replace("NPU_MONITOR_START=true", "NPU_MONITOR_START=false")
        .replace("NPU_MONITOR_STOP=false", "NPU_MONITOR_STOP=true")
    )


def _validate(cfg: MonitorConfig) -> None:
    """全量校验，供测试与需要一次性校验两侧参数的场景使用。"""
    validate(cfg)


def _host_name() -> str:
    try:
        return socket.gethostname() or "localhost"
    except OSError:
        return "localhost"


def _user_name() -> str:
    getuid = getattr(os, "getuid", None)
    if getuid is None:
        return os.environ.get("USERNAME") or "unknown"
    try:
        import pwd

        return pwd.getpwuid(getuid()).pw_name
    except (ImportError, KeyError):
        return str(getuid())


def _log_dir(cfg: MonitorConfig) -> Path:
    """日志目录：默认采集产物目录下的 log 子目录，可用 ``MSMONITOR_LOG_PATH`` 覆盖。"""
    override = os.environ.get("MSMONITOR_LOG_PATH", "").strip()
    return Path(os.path.expanduser(override)) if override else Path(cfg.output_dir) / "log"


def _log_path(cfg: MonitorConfig, name: str) -> Path:
    """本次拉起子进程要写的日志：``msmonitor_<name>.<host>.<user>.log.<时间>``。

    命名对齐训练侧 glog，每次 start 一个文件，避免多用户、多次运行混在同一个文件里。
    """
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return _log_dir(cfg) / "msmonitor_{}.{}.{}.log.{}".format(name, _host_name(), _user_name(), stamp)


def _latest_log_path(cfg: MonitorConfig, name: str) -> Path:
    """最新一份日志，用于打印已运行进程的日志位置；没有历史日志时返回本次将使用的路径。"""
    try:
        files = sorted(_log_dir(cfg).glob("msmonitor_{}.*.log.*".format(name)))
    except OSError:
        files = []
    return files[-1] if files else _log_path(cfg, name)


PRUNE_LOG_FILES = 5


def _prune_logs(log_dir: Path, name: str) -> None:
    """每次拉起都会新建日志，只保留最近 PRUNE_LOG_FILES 份，避免无限堆积。"""
    try:
        files = sorted(log_dir.glob("msmonitor_{}.*.log.*".format(name)))
    except OSError:
        return
    for old_file in files[:-PRUNE_LOG_FILES]:
        try:
            old_file.unlink()
        except OSError:
            pass


def _collector_log_path(cfg: MonitorConfig) -> Path:
    return _log_path(cfg, "collector")


def _spawn_detached(argv: List[str], log_path: Path, *, token: str = "") -> subprocess.Popen:
    """Start a detached child with stdout, stderr and native logs in the same directory."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, MSMONITOR_LOG_PATH=str(log_path.parent))
    if token:
        env["MSMONITOR_TOKEN"] = token
    with open(log_path, "ab") as log:
        return subprocess.Popen(  # nosec B603
            argv,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=log,
            start_new_session=True,
            close_fds=True,
            env=env,
        )


def _ensure_dir(path: str) -> None:
    """建目录并复用仓内安全校验（拒绝软链目录）。"""
    # 延迟导入：IPCMonitor 包 import 时会加载扩展 .so，避免纯参数解析场景强依赖
    from IPCMonitor.file_manager import FileManager

    try:
        FileManager.make_dir_safety(path)
    except RuntimeError as exc:
        raise ConfigError(str(exc))


def _ensure_collector(cfg: MonitorConfig) -> int:
    """确保 collector 存活，返回其 pid；不存在时拉起并等待就绪。"""
    pid = _running_pid(cfg)
    if pid:
        return pid

    _purge_runtime_files(cfg)
    log_path = _collector_log_path(cfg)
    _prune_logs(log_path.parent, "collector")
    proc = _spawn_detached([sys.executable, "-m", COLLECTOR_MODULE], log_path)

    deadline = time.time() + 10.0
    while time.time() < deadline:
        pid = _running_pid(cfg)
        if pid:
            return pid
        if not _pid_alive(proc.pid):
            break
        time.sleep(0.05)

    _purge_runtime_files(cfg)
    raise ConfigError("failed to start collector, see log: {}".format(log_path))


def _processor_log_path(cfg: MonitorConfig) -> Path:
    return _log_path(cfg, "processor")


def _processor_pid_path(cfg: MonitorConfig) -> Path:
    return state_file(cfg.state_dir, PROCESSOR_PID_FILE)


def _running_processor_pid(cfg: MonitorConfig) -> Optional[int]:
    pid = _read_pid(_processor_pid_path(cfg))
    if pid and _is_module_alive(pid, PROCESSOR_MODULE):
        return pid
    return None


def _processor_argv(cfg: MonitorConfig) -> List[str]:
    """start 的参数原样透传给数据处理子进程，产物目录/上报细节由扩展自行处理。"""
    argv = [
        sys.executable,
        "-m",
        PROCESSOR_MODULE,
        "--output-dir",
        cfg.output_dir,
        "--monitor-target",
        ",".join(cfg.monitor_target),
        "--trace-steps-per-file",
        str(cfg.trace_steps_per_file),
        "--trace-keep-files",
        str(cfg.trace_keep_files),
    ]
    if cfg.job_id:
        argv += ["--job-id", cfg.job_id]
    if cfg.remote_write_url:
        argv += ["--remote-write-url", cfg.remote_write_url]
    if cfg.token_file:
        argv += ["--token-file", cfg.token_file]
    if cfg.ca_file:
        argv += ["--ca-file", cfg.ca_file]
    return argv


def _ensure_processor(cfg: MonitorConfig) -> Optional[int]:
    """拉起数据处理子进程；启动失败只告警，不影响采集。"""
    pid = _running_processor_pid(cfg)
    if pid:
        return pid

    log_path = _processor_log_path(cfg)
    _prune_logs(log_path.parent, "processor")
    # Keep bearer credentials out of argv and state files.
    proc = _spawn_detached(_processor_argv(cfg), log_path, token=cfg.token)
    _processor_pid_path(cfg).write_text(str(proc.pid), encoding="utf-8")

    deadline = time.time() + 0.5
    while time.time() < deadline:
        if proc.poll() is not None:
            try:
                _processor_pid_path(cfg).unlink()
            except OSError:
                pass
            print(
                "warning: data processing process exited after start (code={}), see log: {}".format(
                    proc.returncode, log_path
                ),
                file=sys.stderr,
            )
            return None
        time.sleep(0.05)
    return proc.pid


def _terminate(pid: int, timeout_s: float = 5.0) -> None:
    """SIGTERM 让进程自行收尾，超时仍未退出再 SIGKILL。"""
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError:
        return
    deadline = time.time() + timeout_s
    while time.time() < deadline and _pid_alive(pid):
        time.sleep(0.05)
    if _pid_alive(pid):
        print("warning: process {} did not exit on SIGTERM, sending SIGKILL".format(pid), file=sys.stderr)
        try:
            os.kill(pid, getattr(signal, "SIGKILL", signal.SIGTERM))
        except OSError:
            pass


def _stop_processor(cfg: MonitorConfig, timeout_s: float = 5.0) -> Optional[int]:
    """回收数据处理子进程，返回被回收的 pid。"""
    pid = _running_processor_pid(cfg)
    if pid:
        _terminate(pid, timeout_s)
    try:
        _processor_pid_path(cfg).unlink()
    except OSError:
        pass
    return pid


def cmd_start(args) -> int:
    """拉起 collector（采集控制进程）与数据处理子进程，不下发采集配置。

    对应 ``dynolog daemon``：进程起来后对训练侧的 ``req`` 回空配置，
    PTA 侧解析为空 dict 直接跳过，不会开始采集；``msmonitor collect start``
    下发配置后才真正采集。首次拉起 collector 时参数落盘到 ``.config.json``，
    并原样透传给数据处理子进程。
    """
    cfg = load_config(args)
    validate_output(cfg)

    pid = _running_pid(cfg)
    if pid:
        started = _read_started_config(cfg) or {}
        started_output = started.get("output_dir")
        if started_output and started_output != cfg.output_dir:
            raise ConfigError(
                "msmonitor is already running with output_dir {}; run 'msmonitor stop' before changing it".format(
                    started_output
                )
            )
        # start 只负责拉起一次，重复执行只提示现有实例，不再拉起或补齐任何进程
        print("msmonitor is already running (pid={})".format(pid))
        print("collector log: {}".format(_latest_log_path(cfg, "collector")))
        return EXIT_OK

    validate_collector(cfg)
    _ensure_dir(cfg.state_dir)
    _ensure_dir(cfg.output_dir)

    pid = _ensure_collector(cfg)
    _write_started_config(cfg)
    print("msmonitor started (pid={})".format(pid))
    print("output_dir: {}".format(cfg.output_dir))
    print("collector log: {}".format(_latest_log_path(cfg, "collector")))

    processor_pid = _ensure_processor(cfg)
    if processor_pid:
        print("processor: pid={} log={}".format(processor_pid, _latest_log_path(cfg, "processor")))
    return EXIT_OK


def cmd_collect_start(args) -> int:
    """下发采集配置，output_dir / job_id 复用 ``msmonitor start`` 的取值。"""
    cfg = load_config(args)
    saved = _read_started_config(cfg)
    if not saved or not saved.get("output_dir"):
        raise ConfigError(
            "'msmonitor start' has not been run, or it did not set "
            "--output-dir; run 'msmonitor start --output-dir <dir>' first"
        )
    cfg.output_dir = saved["output_dir"]
    if not cfg.job_id:
        cfg.job_id = saved.get("job_id") or ""
    validate_collect(cfg)

    payload = {
        "enabled": True,
        "reply": build_reply(cfg),
        "stop_reply": build_stop_reply(cfg),
    }

    _ensure_dir(cfg.output_dir)
    pid = _ensure_collector(cfg)

    sock_path = _socket_path(cfg)
    if sock_path is None:
        raise ConfigError("collector socket file not found (pid={})".format(pid))
    ok, reason = _send_ctrl(sock_path, payload)
    if not ok:
        raise ConfigError(
            "failed to send collection config to collector (pid={}, socket={}): {}".format(pid, sock_path, reason)
        )

    print("collection started (pid={})".format(pid))
    print("output_dir: {}".format(cfg.output_dir))
    print("activity_kind: {}".format(",".join(cfg.activity_kind or ())))
    print(
        "note: a training process that failed dyno registration will not pick up this config",
    )
    return EXIT_OK


def cmd_collect_stop(args) -> int:
    """下发停止配置，collector 保持运行。"""
    cfg = load_config(args)
    pid = _running_pid(cfg)
    if not pid:
        print("msmonitor is not running")
        return EXIT_OK

    sock_path = _socket_path(cfg)
    if sock_path is None:
        raise ConfigError("collector socket file not found (pid={})".format(pid))
    ok, reason = _send_ctrl(sock_path, {"enabled": False})
    if not ok:
        raise ConfigError(
            "failed to send stop config to collector (pid={}, socket={}): {}".format(pid, sock_path, reason)
        )

    print("stop config sent (pid={}), each rank stops collecting after its next poll".format(pid))
    return EXIT_OK


def cmd_stop(args) -> int:
    """关闭 collector。

    先下发停止配置让已启动的 monitor 停下，再 SIGTERM 回收进程。各 rank 是否
    来得及拿到停止配置取决于自己的轮询间隔；需要确定性停止时先执行
    ``msmonitor collect stop``。
    """
    cfg = load_config(args)
    processor_pid = _stop_processor(cfg)
    pid = _running_pid(cfg)
    if not pid:
        _purge_runtime_files(cfg)
        _remove_started_config(cfg)
        if processor_pid:
            print("processor stopped (pid={})".format(processor_pid))
        print("msmonitor is not running")
        return EXIT_OK

    sock_path = _socket_path(cfg)
    if sock_path is not None and not _send_ctrl(sock_path, {"enabled": False})[0]:
        print("warning: stop config not delivered; training processes may keep collecting", file=sys.stderr)

    _terminate(pid)
    _purge_runtime_files(cfg)
    _remove_started_config(cfg)
    if processor_pid:
        print("processor stopped (pid={})".format(processor_pid))
    print("collector stopped (pid={})".format(pid))
    return EXIT_OK
