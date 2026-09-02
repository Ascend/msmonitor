#!/usr/bin/env python3
# Copyright (c) 2026, Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the Apache License, Version 2.0  (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""端到端用例：FlashAttention 多流计算 + DCMI 硬件指标采集与导出。

每个 rank 在 K 条计算流上并行下发 FA(SDPA→NPU FA) 与 matmul（world_size>1 时默认流做
all_reduce，可观察计算/通信掩盖），Monitor 采集 Kernel/Communication + DCMI 全硬件层，
save() 导出 xlsx/json/log，校验产物完整性与采集量。

运行（需已安装 msmonitor whl、NPU/CANN 环境）：
    python test/st/test_dcmi_fa_comm_multistream.py

参数用环境变量覆盖以控制时长/规模（CI 建议调小）：
    DCMI_E2E_WORLD_SIZE / STREAMS / STEPS / BATCH / N_HEADS / SEQ / HEAD_DIM / SIZE / DTYPE / INTERVAL
"""

import json
import os
import socket
import tempfile
import unittest

try:
    import torch
    import torch.multiprocessing as mp
    import torch_npu

    _NPU_AVAILABLE = torch.npu.is_available()
except Exception:
    _NPU_AVAILABLE = False

try:
    from msmonitor import ActivityKind, DcmiLayer, Monitor

    _MSMONITOR_AVAILABLE = True
except Exception:
    _MSMONITOR_AVAILABLE = False


class Config:
    """用例参数（环境变量可覆盖）。"""

    WORLD_SIZE = int(os.environ.get("DCMI_E2E_WORLD_SIZE", "1"))
    STREAMS = int(os.environ.get("DCMI_E2E_STREAMS", "3"))
    STEPS = int(os.environ.get("DCMI_E2E_STEPS", "10"))
    BATCH = int(os.environ.get("DCMI_E2E_BATCH", "4"))
    N_HEADS = int(os.environ.get("DCMI_E2E_N_HEADS", "8"))
    SEQ = int(os.environ.get("DCMI_E2E_SEQ", "1024"))
    HEAD_DIM = int(os.environ.get("DCMI_E2E_HEAD_DIM", "64"))
    SIZE = int(os.environ.get("DCMI_E2E_SIZE", "2048"))
    DTYPE = os.environ.get("DCMI_E2E_DTYPE", "float16")
    INTERVAL = int(os.environ.get("DCMI_E2E_INTERVAL", "10"))


def fa_compute(q, k, v, n_heads):
    """FlashAttention: 优先 npu_fusion_attention，回退 SDPA（NPU 上 dispatch 到 FA）。"""
    if hasattr(torch_npu, "npu_fusion_attention"):
        try:
            res = torch_npu.npu_fusion_attention(
                q, k, v, n_heads, input_layout="BNSD", scale=1.0 / (q.shape[-1] ** 0.5)
            )
            return res[0] if isinstance(res, (tuple, list)) else res
        except Exception:
            pass
    return torch.nn.functional.scaled_dot_product_attention(q, k, v)


def run_rank(rank, world_size, cfg, out_dir, master_port):
    os.environ["MASTER_ADDR"] = "127.0.0.1"
    os.environ["MASTER_PORT"] = str(master_port)
    os.environ["RANK"] = str(rank)
    os.environ["WORLD_SIZE"] = str(world_size)

    torch_npu.npu.set_device(rank)
    device = f"npu:{rank}"
    if world_size > 1:
        import torch.distributed as dist

        dist.init_process_group(backend="hccl", rank=rank, world_size=world_size)

    # K 条计算流 + 默认流（world_size>1 时通信用）
    streams = [torch.npu.Stream(device=device) for _ in range(cfg.STREAMS)]
    main_stream = torch.npu.current_stream()

    dtype = getattr(torch, cfg.DTYPE)
    b, n, s, d = cfg.BATCH, cfg.N_HEADS, cfg.SEQ, cfg.HEAD_DIM
    q = torch.randn(b, n, s, d, dtype=dtype, device=device)
    k = torch.randn(b, n, s, d, dtype=dtype, device=device)
    v = torch.randn(b, n, s, d, dtype=dtype, device=device)
    mm = torch.randn(cfg.SIZE, cfg.SIZE, dtype=dtype, device=device)
    ar_tensor = torch.randn(cfg.SIZE, cfg.SIZE, dtype=dtype, device=device)
    acc = torch.zeros(1, dtype=torch.float32, device=device)

    monitor = Monitor()
    monitor.start(
        kinds=[ActivityKind.Kernel, ActivityKind.Communication],
        dcmi_layers=[DcmiLayer.DEVICE, DcmiLayer.AICORE, DcmiLayer.AICPU, DcmiLayer.HBM],
        dcmi_interval_ms=cfg.INTERVAL,
        save_path=out_dir,  # 会话目录直接建在输出目录下，glog 从开始写入其 log/
    )
    try:
        for step in range(cfg.STEPS):
            rid = torch.npu.mstx.range_start(f"step {step}", main_stream)
            # 多流并行下发 FA + matmul
            for s in streams:
                with torch.npu.stream(s):
                    out = fa_compute(q, k, v, n)
                    acc += out.float().sum()
                    acc += torch.matmul(mm, mm).float().sum()
            if world_size > 1:
                dist.all_reduce(ar_tensor, op=dist.ReduceOp.SUM)
            main_stream.synchronize()
            torch.npu.mstx.range_end(rid)
    finally:
        # 计算流未同步，最后几步 FA/matmul 可能仍在跑；先等全部流完成再停采集，
        # 否则 kernel 结束时刻晚于 DCMI 停止时刻，timeline 出现尾部空洞
        for s in streams:
            s.synchronize()
        monitor.stop()
        if world_size > 1:
            import torch.distributed as dist

            dist.destroy_process_group()

    result = monitor.get_result()
    counts = {getattr(kk, "name", str(kk)): len(vv) for kk, vv in result.items()}
    print(f"[rank{rank}] 采集量: {counts}", flush=True)
    monitor.save()
    print(f"[rank{rank}] 导出完成: {out_dir}/msmonitor_*", flush=True)


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


@unittest.skipUnless(_MSMONITOR_AVAILABLE, "msmonitor whl 未安装")
@unittest.skipUnless(_NPU_AVAILABLE, "NPU 不可用")
class TestDcmiFaCommMultistream(unittest.TestCase):
    def test_run_collect_and_export(self):
        cfg = Config()
        if cfg.WORLD_SIZE > torch.npu.device_count():
            self.skipTest(f"world_size {cfg.WORLD_SIZE} > 可见 NPU 数 {torch.npu.device_count()}")

        out_dir = tempfile.mkdtemp(prefix="dcmi_e2e_")
        print(f"[e2e] spawn {cfg.WORLD_SIZE} rank, {cfg.STREAMS} 计算流/rank, {cfg.STEPS} 步, out={out_dir}")
        mp.spawn(run_rank, args=(cfg.WORLD_SIZE, cfg, out_dir, _free_port()), nprocs=cfg.WORLD_SIZE, join=True)

        # 每个 rank 一个会话目录，产物齐全
        sessions = sorted(d for d in os.listdir(out_dir) if d.startswith("msmonitor_"))
        self.assertEqual(
            len(sessions), cfg.WORLD_SIZE, f"期望 {cfg.WORLD_SIZE} 个会话目录, 实际 {len(sessions)}: {sessions}"
        )
        for name in sessions:
            folder = os.path.join(out_dir, name)
            self.assertTrue(
                os.path.isfile(os.path.join(folder, "monitor_result.xlsx")), f"{folder} 缺 monitor_result.xlsx"
            )
            json_path = os.path.join(folder, "monitor_result.json")
            self.assertTrue(os.path.isfile(json_path), f"{folder} 缺 monitor_result.json")
            log_dir = os.path.join(folder, "log")
            self.assertTrue(os.path.isdir(log_dir), f"{folder} 缺 log/")
            self.assertTrue(
                any(fn.startswith("msmonitor_") and fn.endswith(".log") for fn in os.listdir(log_dir)),
                f"{log_dir} 缺 glog 日志",
            )
            # Chrome Trace 内 DCMI 曲线与 kernel 事件非空
            with open(json_path, "r", encoding="utf-8") as f:
                trace = json.load(f)
            counters = [e for e in trace["traceEvents"] if e.get("ph") == "C"]
            kernels = [e for e in trace["traceEvents"] if e.get("ph") == "X"]
            self.assertGreater(len(counters), 0, f"{json_path} 无 DCMI counter 事件")
            self.assertGreater(len(kernels), 0, f"{json_path} 无 kernel 事件")


if __name__ == "__main__":
    unittest.main()
