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

"""Configuration model and shared runtime paths for the msmonitor CLI.

配置只来自命令行参数，未显式指定的项使用内置默认值。状态目录下的运行时
文件名常量与路径工具也放在这里，供 CLI 与 collector/processor 子进程共用。
"""

from __future__ import annotations

import math
import os
import signal
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence
from urllib.parse import urlsplit

# 采集数据只导出 Jsonl，仅支持这些类型。
JSONL_EXPORTABLE_KINDS = (
    "Marker",
    "Kernel",
    "API",
    "Communication",
    "AclAPI",
    "NodeAPI",
    "RuntimeAPI",
)
# 产物去向：trace 落盘到 output_dir，metric 上报数据中心
MONITOR_TARGETS = ("trace", "metric")
ROTATE_LINES_MIN = 100
ROTATE_LINES_MAX = 500000
ROTATE_FILES_MIN = 2
# 与 dyno npu-monitor 的 validate_string_max_len 保持一致
STRING_MAX_LEN = 1024

# 状态目录及其运行时文件，CLI 与 collector/processor 共用同一份定义
DEFAULT_STATE_DIR = "~/.msmonitor"
READY_FILE = ".ready"
SOCKET_FILE = ".socket"
CONFIG_FILE = ".config.json"
PROCESSOR_PID_FILE = ".processor.pid"


def state_file(state_dir: str, name: str) -> Path:
    """Return ``<state_dir>/<name>`` with ``~`` expanded."""
    return Path(os.path.expanduser(state_dir)) / name


def remove_state_files(state_dir: str, names: Sequence[str]) -> None:
    """Remove runtime files, ignoring files that are already gone."""
    for name in names:
        try:
            state_file(state_dir, name).unlink()
        except OSError:
            pass


def install_stop_signals() -> threading.Event:
    """Set up SIGTERM/SIGINT handlers and return the event they set."""
    stop = threading.Event()

    def _on_signal(signum, frame):  # noqa: ARG001 - signal 签名固定
        stop.set()

    signal.signal(signal.SIGTERM, _on_signal)
    signal.signal(signal.SIGINT, _on_signal)
    return stop


class ConfigError(Exception):
    """Raised when the resolved configuration is not usable."""


def split_csv(value: Any) -> List[str]:
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        return [str(item).strip() for item in value if str(item).strip()]
    return [item.strip() for item in str(value).split(",") if item.strip()]


def normalize_monitor_target(value: Any) -> List[str]:
    """把 trace/metric 归一化成小写并去重，拒绝未知去向。"""
    targets = []
    for item in split_csv(value):
        target = item.lower()
        if target not in MONITOR_TARGETS:
            raise ConfigError(
                "unsupported monitor target: {!r}, expected one of: {}".format(item, ", ".join(MONITOR_TARGETS))
            )
        if target not in targets:
            targets.append(target)
    if not targets:
        raise ConfigError("--monitor-target must not be empty")
    return targets


@dataclass
class MonitorConfig:
    job_id: str = ""

    state_dir: str = ""
    output_dir: str = ""

    activity_kind: List[str] = field(default_factory=lambda: ["Marker", "Communication", "Kernel"])
    report_interval_s: int = 60
    duration_s: float = 0.0
    json_rotate_log_lines: Optional[int] = None
    json_rotate_log_files: Optional[int] = None
    # 数据处理阶段的 trace 产物切分：每个 trace 文件包含的 step 数、保留的 trace 文件数。
    trace_steps_per_file: int = 5
    trace_keep_files: int = 100

    server_host: str = ""
    # Legacy endpoint metadata. The Remote Write URL determines the destination.
    server_port: int = 0
    # 默认同时输出 trace 与 metric；只写其中一个表示只输出该项。
    monitor_target: List[str] = field(default_factory=lambda: ["trace", "metric"])
    remote_write_url: str = ""
    token_file: str = ""
    # 明文 token，与 token_file 二选一；通过环境变量传给子进程，不写入命令行和状态文件。
    token: str = ""
    ca_file: str = ""


def _apply_values(cfg: MonitorConfig, values: Dict[str, Any]) -> None:
    for key, value in values.items():
        if value is None:
            continue
        try:
            if key == "activity_kind":
                cfg.activity_kind = split_csv(value)
            elif key == "duration_s":
                cfg.duration_s = float(value)
            elif key == "monitor_target":
                cfg.monitor_target = normalize_monitor_target(value)
            elif key in (
                "report_interval_s",
                "json_rotate_log_lines",
                "json_rotate_log_files",
                "trace_steps_per_file",
                "trace_keep_files",
                "server_port",
            ):
                setattr(cfg, key, int(value))
            else:
                setattr(cfg, key, str(value))
        except (TypeError, ValueError):
            raise ConfigError("invalid value for {}: {!r}".format(key, value))


def resolve(cli_values: Dict[str, Any]) -> MonitorConfig:
    """Merge built-in defaults and command line values."""
    cfg = MonitorConfig()
    _apply_values(cfg, cli_values)
    return cfg


def validate_output(cfg: MonitorConfig) -> None:
    """校验并规范化进程级参数（output_dir / state_dir）。"""
    if not cfg.output_dir:
        raise ConfigError("missing required argument: --output-dir")

    output_path = Path(cfg.output_dir).expanduser().resolve()
    state_path = Path(cfg.state_dir).expanduser().resolve()
    if state_path == output_path:
        raise ConfigError("output directory must not be the msmonitor state directory")

    # reply 是逐行 KEY=VALUE，值里出现 '=' 或换行会把整条配置解析坏
    if any(ch in str(output_path) for ch in "=\n\r"):
        raise ConfigError("--output-dir must not contain '=' or line breaks")
    cfg.output_dir = str(output_path)
    cfg.state_dir = str(state_path)


def validate_collector(cfg: MonitorConfig) -> None:
    """校验 collector 侧参数。"""
    if cfg.token and cfg.token_file:
        raise ConfigError("--token and --token-file are mutually exclusive; specify only one")
    if cfg.server_port and not 0 < cfg.server_port < 65536:
        raise ConfigError("--server-port must be within [1, 65535]")
    if "metric" in cfg.monitor_target:
        missing = [
            name
            for name, value in (
                ("--remote-write-url", cfg.remote_write_url),
                ("--token or --token-file", cfg.token or cfg.token_file),
            )
            if not value
        ]
        if missing:
            raise ConfigError("--monitor-target metric requires: {}".format(", ".join(missing)))
        try:
            endpoint = urlsplit(cfg.remote_write_url)
            valid_endpoint = endpoint.scheme == "https" and endpoint.hostname and endpoint.username is None
            port = endpoint.port
        except ValueError:
            valid_endpoint = False
            port = None
        if not valid_endpoint or port == 0 or any(ch.isspace() for ch in cfg.remote_write_url):
            raise ConfigError("--remote-write-url must be an HTTPS URL without embedded credentials")
    if cfg.trace_steps_per_file < 0:
        raise ConfigError("--trace-steps-per-file must not be negative")
    if cfg.trace_keep_files < 0:
        raise ConfigError("--trace-keep-files must not be negative")


def validate_collect(cfg: MonitorConfig) -> None:
    """校验采集侧参数。"""
    if not cfg.activity_kind:
        raise ConfigError("--mspti-activity-kind must not be empty")

    # 与 dyno 的 u32 保持一致：0 合法，表示沿用训练侧默认
    if cfg.report_interval_s < 0 or cfg.report_interval_s > 0xFFFFFFFF:
        raise ConfigError("--report-interval-s must be within [0, 4294967295] (uint32)")

    if not math.isfinite(cfg.duration_s) or cfg.duration_s < 0:
        raise ConfigError("--duration must be finite and non-negative")

    # 采集导出固定为 Jsonl，只支持可导出文本流的类型
    unsupported = [kind for kind in cfg.activity_kind if kind not in JSONL_EXPORTABLE_KINDS]
    if unsupported:
        raise ConfigError("unsupported activity type(s): {}".format(", ".join(unsupported)))

    if cfg.json_rotate_log_lines is not None and not (
        ROTATE_LINES_MIN <= cfg.json_rotate_log_lines <= ROTATE_LINES_MAX
    ):
        raise ConfigError("--json-rotate-log-lines must be within [{}, {}]".format(ROTATE_LINES_MIN, ROTATE_LINES_MAX))

    if cfg.json_rotate_log_files is not None and (
        cfg.json_rotate_log_files != -1 and cfg.json_rotate_log_files < ROTATE_FILES_MIN
    ):
        raise ConfigError("--json-rotate-log-files must be -1 (no cleanup) or at least {}".format(ROTATE_FILES_MIN))


def validate(cfg: MonitorConfig) -> None:
    """Validate the resolved configuration, raising ConfigError on errors."""
    validate_output(cfg)
    validate_collect(cfg)
    validate_collector(cfg)
