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

"""msmonitor 采集控制进程（collector）。

在 ``$HOME/dynolog[_<uid>].sock`` 上应答训练侧 PTA 监控进程：

* 收到 ``req``                → 回内存里的 KEY=VALUE 文本，同一客户端只回一次；
                                尚未下发配置时回空串（对应 dynolog daemon 未配置状态）
* 收到 ``ctxt`` / 状态上报等  → 直接丢弃（对齐 dynolog 只维护内存状态、不落盘的语义）
* 收到 ``ctrl``               → 由 ``msmonitor collect start/stop`` 直连本 socket 下发，
                                更新内存配置并回 ack

配置只存在于本进程内存，与 dynolog daemon 的 ``jobs_`` 语义一致，没有配置文件轮询。

``stop`` 只切配置、不杀进程：PTA 只有收到 ``NPU_MONITOR_STOP=true`` 才会停
MSPTI 采集，所以本进程收到 ``enabled=false`` 后继续常驻应答，保证任意轮询
间隔的 rank 都能拿到停止配置。回收进程只能由 ``msmonitor stop`` 触发。
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path
from typing import Dict, Optional

from .config import (
    DEFAULT_STATE_DIR,
    READY_FILE,
    SOCKET_FILE,
    install_stop_signals,
    remove_state_files,
)

STATE_DIR = Path(os.path.expanduser(DEFAULT_STATE_DIR))
# 空队列时的等待间隔。消息本身由训练进程按需请求，50ms 延迟无感，
# 相比 1ms 忙轮询把空闲系统调用降到 1/50。
POLL_INTERVAL_S = 0.05

MSG_CTRL = "ctrl"
MSG_REQ = "req"


def apply_ctrl(state: Dict[str, Optional[object]], doc) -> bool:
    """把 CLI 下发的控制消息应用到内存状态；返回是否已生效。"""
    if not isinstance(doc, dict):
        return False
    if doc.get("enabled"):
        reply = doc.get("reply")
        if not reply:
            return False
        state["reply"] = reply
        state["stop_reply"] = doc.get("stop_reply") or state.get("stop_reply")
    elif state.get("stop_reply"):
        # stop：沿用 start 时下发的 stop_reply，保证 rank 收到的是完整停止配置
        state["reply"] = state["stop_reply"]
    # 配置切换后，所有 rank 需要重新取一次
    state.setdefault("delivered", set()).clear()
    return True


def main() -> int:
    import pwd

    state_dir = STATE_DIR
    state_dir.mkdir(parents=True, exist_ok=True)

    from IPCMonitor import ipcmonitor_C_module as ipc

    if not ipc.serve_start("dynolog"):
        print(
            "failed to bind dynolog socket, another collector may be running (state_dir={})".format(state_dir),
            file=sys.stderr,
        )
        return 1

    # The native IPC endpoint uses the account home even when HOME is overridden.
    sock_path = Path(pwd.getpwuid(os.getuid()).pw_dir) / "{}.sock".format(ipc.serve_name())
    (state_dir / SOCKET_FILE).write_text(str(sock_path), encoding="utf-8")
    (state_dir / READY_FILE).write_text(str(os.getpid()), encoding="utf-8")
    print(
        "=== msmonitor collector start {} pid={} socket={} ===".format(
            time.strftime("%Y-%m-%d %H:%M:%S"), os.getpid(), sock_path
        ),
        flush=True,
    )

    stop = install_stop_signals()

    state: Dict[str, Optional[object]] = {
        "reply": None,
        "stop_reply": None,
        "delivered": set(),
    }
    try:
        while not stop.is_set():
            msg = ipc.serve_poll()
            if msg is None:
                time.sleep(POLL_INTERVAL_S)
                continue

            msg_type, payload, src = msg
            if msg_type == MSG_CTRL:
                try:
                    doc = json.loads(payload)
                except (TypeError, ValueError):
                    ipc.serve_reply(src, MSG_CTRL, "err:bad payload")
                    print("ctrl from {}: bad payload".format(src), flush=True)
                    continue
                ok = apply_ctrl(state, doc)
                ipc.serve_reply(src, MSG_CTRL, "ok" if ok else "err:incomplete")
                # 把收到的配置压成一行写入日志，排查"配置有没有到、内容是什么"只看这里
                print(
                    "ctrl from {}: enabled={} result={} reply={}".format(
                        src,
                        doc.get("enabled"),
                        "ok" if ok else "err:incomplete",
                        " ".join(str(doc.get("reply") or "").split()),
                    ),
                    flush=True,
                )
                continue

            delivered = state["delivered"]
            if msg_type == MSG_REQ:
                if not state["reply"] or src in delivered:
                    # 未下发配置或已投递过：回空 payload 而不是不回，否则
                    # PTA 的 PollRecvMessage 重试到超时后打 WARNING，仍然拿不到配置。
                    # 回空后 dyno_str_to_json 得到空 dict，训练侧直接 continue。
                    ipc.serve_reply(src, msg_type, "")
                elif ipc.serve_reply(src, msg_type, state["reply"]):
                    if src not in delivered:
                        # 每个训练进程只记首次投递，避免同一进程重复请求刷屏
                        print("req from {}: config delivered".format(src), flush=True)
                    delivered.add(src)
    finally:
        ipc.serve_stop()
        remove_state_files(str(state_dir), (READY_FILE, SOCKET_FILE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
