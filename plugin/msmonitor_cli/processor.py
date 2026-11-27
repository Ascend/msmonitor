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

"""数据处理子进程入口。

``msmonitor start`` 以 ``sys.executable -m msmonitor_cli.processor`` 拉起本进程，
把 start 的参数原样转交给编译进 ``IPCMonitor_C`` 扩展的数据处理组件；
``msmonitor stop`` 通过 SIGTERM 通知本进程调用 ``process_stop`` 排空退出。

扩展侧约定（非阻塞）：

* ``process_start(config: dict)``  按 rank 起处理线程后立即返回；
* ``process_stop()``               置停止位并等待各 rank 线程退出。

config 的键为 snake_case：采集产物目录以 ``input_dir`` 传入（对应 ``--output-dir``），
产物目录与上报细节由扩展内负责，CLI 只传参。
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import List, Optional

from .config import MonitorConfig, install_stop_signals, normalize_monitor_target

POLL_INTERVAL_S = 1.0
# 默认值只存于 MonitorConfig，避免 CLI / 子进程两处漂移
_DEFAULTS = MonitorConfig()


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="msmonitor processor")
    parser.add_argument(
        "--output-dir",
        required=True,
        help="collection output directory, i.e. the input directory of the data processing component",
    )
    parser.add_argument("--job-id", default=_DEFAULTS.job_id)
    parser.add_argument(
        "--trace-steps-per-file",
        type=int,
        default=_DEFAULTS.trace_steps_per_file,
        help="steps contained in each trace file",
    )
    parser.add_argument(
        "--trace-keep-files",
        type=int,
        default=_DEFAULTS.trace_keep_files,
        help="trace files to keep; older files are removed",
    )
    parser.add_argument("--remote-write-url", default=_DEFAULTS.remote_write_url)
    parser.add_argument("--token-file", default=_DEFAULTS.token_file)
    parser.add_argument("--ca-file", default=_DEFAULTS.ca_file)
    parser.add_argument(
        "--monitor-target",
        default=",".join(_DEFAULTS.monitor_target),
        help="output targets: trace writes files to --output-dir, metric reports to the data center",
    )
    return parser


def _config(args) -> dict:
    targets = normalize_monitor_target(args.monitor_target)
    return {
        "input_dir": args.output_dir,
        "job_id": args.job_id,
        "enable_trace": "trace" in targets,
        "enable_metric": "metric" in targets,
        "trace_steps_per_file": args.trace_steps_per_file,
        "trace_keep_files": args.trace_keep_files,
        "remote_write_url": args.remote_write_url,
        "token_file": args.token_file,
        # 明文 token 由 start 通过环境变量传递，不出现在命令行里
        "token": os.environ.get("MSMONITOR_TOKEN", ""),
        "ca_file": args.ca_file,
    }


def main(argv: Optional[List[str]] = None) -> int:
    args = _build_parser().parse_args(argv)

    from IPCMonitor import ipcmonitor_C_module as ipc

    start = getattr(ipc, "process_start", None)
    stop = getattr(ipc, "process_stop", None)
    if start is None or stop is None:
        print(
            "IPCMonitor extension does not provide process_start/process_stop, data processing not started",
            file=sys.stderr,
        )
        return 1

    stop_signal = install_stop_signals()

    try:
        if not start(_config(args)):
            print(
                "failed to start data processing component: already running",
                file=sys.stderr,
            )
            return 1
    except Exception as exc:  # 参数不兼容或组件内部失败时给出明确日志
        print("process_start failed: {}".format(exc), file=sys.stderr)
        return 1

    # 把 start 传入的进程级参数一次性打进日志：排查"当时用的是哪套配置"只看这一行；
    # remote_write_url 可能带查询串，只记录是否配置。
    print(
        "=== msmonitor processor start pid={} input_dir={} job_id={} "
        "trace_steps_per_file={} trace_keep_files={} monitor_target={} remote_write={} ===".format(
            os.getpid(),
            args.output_dir,
            args.job_id,
            args.trace_steps_per_file,
            args.trace_keep_files,
            args.monitor_target,
            bool(args.remote_write_url),
        ),
        flush=True,
    )
    while not stop_signal.is_set():
        # 信号处理在主线程执行，用短 sleep 保证及时响应 SIGTERM
        time.sleep(POLL_INTERVAL_S)

    stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
