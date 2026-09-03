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

"""Chrome Trace JSON exporter for Monitor activity data.

生成兼容 chrome://tracing / Perfetto UI 的 trace 文件，按设备-硬件层分进程呈现：

每个设备 6 个进程（进程名 "Device {d} · {层}"，自上而下）：
  0 Ascend Hardware   —— mspti kernel/communication 耗时条，每个 stream 一个泳道(tid=streamId)
  1 Power/Temp        —— DEVICE 层指标曲线（与 kernel 泳道分离，保证泳道在曲线上方）
  2 AICore            —— AI Core 频率/利用率等曲线
  3 AICPU             —— AICPU 最大/当前频率、利用率曲线
  4 HBM               —— 片上内存频率/容量/用量/带宽利用率/温度曲线
  5 Overlap Analysis  —— 计算/通信掩盖分析：Computing / Communication(Overlapped) /
                         Communication(Not Overlapped) / Free 四行时间片（对齐 MindStudio profiler）

pid 为合成值（1_000_000 + 设备号*6 + 层序号），不参与显示语义；Host 进程用真实 pid，
排序按 process_sort_index（Host=0 置顶，设备进程按 1000+序号 排其下）。

kernel/comm/API/Marker 使用 "X" 完整事件（耗时条），DCMI 指标使用 "C" counter 事件（曲线）。
流式写降低大样本场景内存占用；指标元数据（层/展示名/单位）由 C++ 注册表传入，单一数据源。
"""

import json
import os
import socket

from .file_manager import FileManager

NS_TO_US = 1000.0

# 每设备进程数（含 Overlap Analysis）
PROCESSES_PER_DEVICE = 6
# 合成 pid 基线：避免与真实 OS pid 混淆（真实 pid 一般 < 10^6）
PID_BASE = 1_000_000
# 设备进程序号 → 名称
PROCESS_ORDER = {0: "Ascend Hardware", 1: "Power/Temp", 2: "AICore", 3: "AICPU", 4: "HBM", 5: "Overlap Analysis"}
# DCMI 层 → 设备进程序号（DEVICE 层曲线单独成进程，置于 Ascend Hardware 之下）
LAYER_TO_ORDER = {0: 1, 1: 2, 2: 3, 3: 4}


def _device_pid(device_id, order):
    return PID_BASE + int(device_id) * PROCESSES_PER_DEVICE + int(order)


class ChromeTraceExporter:
    @staticmethod
    def _is_dcmi_data(data):
        # DcmiSample 有 .value/.timestampNs 但无 .endNs；activity 行有 .endNs
        try:
            first = next(iter(data))
        except (StopIteration, TypeError):
            return False
        return hasattr(first, "value") and not hasattr(first, "endNs")

    @staticmethod
    def _row_ts_ns(row):
        # 取一条记录的起始时间(ns)。DCMI 用 timestampNs，算子类用 startNs。
        return row.timestampNs if hasattr(row, "timestampNs") else row.startNs

    @classmethod
    def _base_timestamp_ns(cls, result):
        # trace 起始时间(全局最小 ns)。所有事件 ts 统一减去它再转 us，
        # 避免大绝对时间戳(epoch ns/1000 ≈ 1.7e15 us)在 double 下 ULP≈250ns
        # 导致相邻 kernel 时间片视觉交叉。相对 ts 量级小，精度恢复到亚 ns。
        base = None
        for data in result.values():
            try:
                first = next(iter(data))
            except (StopIteration, TypeError):
                continue
            ts = cls._row_ts_ns(first)
            if ts is None:
                continue
            if base is None or ts < base:
                base = ts
        return base if base is not None else 0

    # ---------- Overlap Analysis（计算/通信掩盖） ----------

    @staticmethod
    def _merge_intervals(intervals):
        """区间合并（重叠/相接合并），返回有序 [(start, end)]。"""
        iv = sorted(intervals)
        out = []
        for s, e in iv:
            if out and s <= out[-1][1]:
                out[-1] = (out[-1][0], max(out[-1][1], e))
            else:
                out.append((s, e))
        return out

    @staticmethod
    def _intersects(cs, ce, merged):
        """通信区间 [cs,ce) 是否与任一合并后的计算区间相交（已按 start 排序）。"""
        for ks, ke in merged:
            if ks >= ce:
                break
            if ke > cs:
                return True
        return False

    @classmethod
    def _overlap_events(cls, result, base_ns):
        """按设备计算掩盖关系，产出 Overlap Analysis 进程的 X 事件：
        tid=0 Computing / tid=1 Communication(Overlapped) / tid=2 Communication(Not Overlapped)
        / tid=3 Free。对齐 MindStudio profiler 的 Overlap Analysis 呈现。
        """
        from collections import defaultdict

        kernels = defaultdict(list)  # dev -> [(startNs, endNs)]
        comms = defaultdict(list)
        for kind, data in result.items():
            cat = getattr(kind, "name", str(kind))
            if cls._is_dcmi_data(data):
                continue
            for row in data:
                t = row.to_tuple()
                if cat == "Kernel":
                    kernels[t[3]].append((row.startNs, row.endNs))
                elif cat == "Communication":
                    comms[t[3]].append((row.startNs, row.endNs))

        out = []
        for dev in sorted(set(kernels) | set(comms)):
            computing = cls._merge_intervals(kernels.get(dev, []))
            overlapped = []
            not_overlapped = []
            for cs, ce in sorted(comms.get(dev, [])):
                if cls._intersects(cs, ce, computing):
                    overlapped.append((cs, ce))
                else:
                    not_overlapped.append((cs, ce))
            # Free = 有界时间窗内（计算∪通信）的间隙
            busy = cls._merge_intervals(comms.get(dev, []) + kernels.get(dev, []))
            free = []
            if busy:
                prev = busy[0][0]
                for s, e in busy:
                    if s > prev:
                        free.append((prev, s))
                    prev = max(prev, e)
                end = busy[-1][1]
                if end > prev:
                    free.append((prev, end))

            pid = _device_pid(dev, 5)
            for tid, name, ivs in (
                (0, "Computing", computing),
                (1, "Communication(Overlapped)", overlapped),
                (2, "Communication(Not Overlapped)", not_overlapped),
                (3, "Free", free),
            ):
                for s, e in ivs:
                    out.append(
                        {
                            "name": name,
                            "cat": "Overlap",
                            "ph": "X",
                            "ts": (s - base_ns) / NS_TO_US,
                            "dur": (e - s) / NS_TO_US,
                            "pid": pid,
                            "tid": tid,
                            "args": {"deviceId": int(dev)},
                        }
                    )
        return out

    # ---------- 主导出 ----------

    @classmethod
    def export(cls, file_path, result, metric_meta=None, dcmi_status=None):
        """导出 Chrome Trace JSON。

        Args:
            file_path: 输出文件路径(.json)。
            result: get_result() 的返回字典。
            metric_meta: {int(kind): {"layer": int, "displayName": str, "unit": str}}，
                来自内部 `_get_dcmi_metric_meta()`；缺省时按枚举名兜底。
            dcmi_status: _get_dcmi_status() 返回的 DFX 状态（写入 metadata，可空）。
        """
        if not file_path or not isinstance(file_path, str):
            print("[ERROR] Invalid chrome trace file path")
            return
        if not file_path.endswith(".json"):
            file_path += ".json"
        if os.path.exists(file_path):
            print(f"[WARNING] File already exists: {file_path}, will be overwritten")
        FileManager.create_file_by_path(file_path)

        base_ns = cls._base_timestamp_ns(result)
        print(f"[INFO] Start to save chrome trace file: {file_path} (baseNs={base_ns})")
        try:
            with open(file_path, "w", encoding="utf-8") as f:
                f.write('{"traceEvents":[')
                first = True

                device_pids = set()  # 设备侧 pid（kernel/comm/设备 marker + DCMI + Overlap）
                stream_threads = set()  # (pid, tid) 来自 kernel/comm（stream 泳道）
                all_pids = set()  # 所有出现过的 pid

                def write(ev):
                    nonlocal first
                    cls._write_event(f, ev, first)
                    first = False

                for kind, data in result.items():
                    name = getattr(kind, "name", str(kind))
                    if cls._is_dcmi_data(data):
                        gen = cls._dcmi_events(data, base_ns, metric_meta)
                    else:
                        gen = cls._activity_events(name, data, base_ns)
                    for ev in gen:
                        write(ev)
                        pid = ev.get("pid")
                        if pid is not None:
                            all_pids.add(pid)
                        cat = ev.get("cat")
                        if cat in ("Kernel", "Communication"):
                            device_pids.add(pid)
                            stream_threads.add((pid, ev.get("tid")))
                        elif cat == "DCMI" or (cat == "Marker" and ev.get("args", {}).get("sourceKind") != "Host"):
                            device_pids.add(pid)

                # Overlap Analysis（每设备计算/通信掩盖行，置于最下方）
                overlap_events = cls._overlap_events(result, base_ns)
                overlap_pids = set()
                overlap_threads = set()  # (pid, tid) → 行名
                for ev in overlap_events:
                    write(ev)
                    all_pids.add(ev["pid"])
                    device_pids.add(ev["pid"])
                    overlap_pids.add(ev["pid"])
                    overlap_threads.add((ev["pid"], ev["tid"], ev["name"]))

                def emit(m):
                    nonlocal first
                    cls._write_event(f, m, first)
                    first = False

                # 进程元数据：Host 置顶（sort_index=0），设备进程按 1000+序号 排其下。
                # 命名 "Device {d} · {层}"；pid 为合成值（1M+），仅供排序/关联，不展示语义。
                for pid in sorted(all_pids, key=lambda x: (x is None, x)):
                    is_dev = pid in device_pids
                    if is_dev:
                        dev_id = (pid - PID_BASE) // PROCESSES_PER_DEVICE
                        order = (pid - PID_BASE) % PROCESSES_PER_DEVICE
                        layer_name = PROCESS_ORDER.get(order, "Unknown")
                        name = f"Device {dev_id} · {layer_name}"
                        sort_index = 1000 + int(dev_id) * PROCESSES_PER_DEVICE + order
                    else:
                        name = "Host"
                        sort_index = 0
                    emit({"name": "process_name", "ph": "M", "pid": pid, "tid": 0, "args": {"name": name}})
                    emit(
                        {
                            "name": "process_sort_index",
                            "ph": "M",
                            "pid": pid,
                            "tid": 0,
                            "args": {"sort_index": sort_index},
                        }
                    )
                # 线程元数据：kernel/comm 的 stream 泳道
                for pid, tid in sorted(stream_threads, key=lambda x: ((x[0] is None, x[0]), (x[1] is None, x[1]))):
                    emit({"name": "thread_name", "ph": "M", "pid": pid, "tid": tid, "args": {"name": f"Stream {tid}"}})
                    emit(
                        {
                            "name": "thread_sort_index",
                            "ph": "M",
                            "pid": pid,
                            "tid": tid,
                            "args": {"sort_index": int(tid)},
                        }
                    )
                # 线程元数据：Overlap Analysis 行（Computing/通信/Free）
                for pid, tid, tname in sorted(overlap_threads, key=lambda x: (x[0], x[1])):
                    emit({"name": "thread_name", "ph": "M", "pid": pid, "tid": tid, "args": {"name": tname}})
                    emit({"name": "thread_sort_index", "ph": "M", "pid": pid, "tid": tid, "args": {"sort_index": tid}})

                metadata = {"host": socket.gethostname(), "pid": os.getpid(), "traceStartNs": base_ns}
                if dcmi_status:
                    metadata["dcmi"] = dcmi_status
                f.write('],"metadata":' + json.dumps(metadata) + '}')

            print(f"[INFO] Chrome trace file saved: {file_path}")
        except Exception as err:
            print(f"[ERROR] Failed to save chrome trace: {file_path}, error: {err}")

    @staticmethod
    def _write_event(f, ev, first):
        if not first:
            f.write(",")
        f.write(json.dumps(ev))

    @classmethod
    def _activity_events(cls, cat, items, base_ns):
        # API/AclAPI/NodeAPI/RuntimeAPI -> X event with pid/tid（host 进程）
        # Kernel/Communication -> X event，pid=设备 Ascend Hardware 进程，tid=streamId（泳道）
        # Marker：Host 源用真实 pid/tid，Device 源进设备进程
        api_like = cat in ("API", "AclAPI", "NodeAPI", "RuntimeAPI")
        for row in items:
            t = row.to_tuple()
            start_us = (row.startNs - base_ns) / NS_TO_US
            dur_us = (row.endNs - row.startNs) / NS_TO_US
            if cat == "Kernel":
                # t = (name, start_us, end_us, deviceId, streamId, corrId, type)
                yield {
                    "name": str(t[0]),
                    "cat": "Kernel",
                    "ph": "X",
                    "ts": start_us,
                    "dur": dur_us,
                    "pid": _device_pid(t[3], 0),
                    "tid": int(t[4]),
                    "args": {
                        "type": str(t[6]),
                        "deviceId": int(t[3]),
                        "streamId": int(t[4]),
                        "correlationId": int(t[5]),
                    },
                }
            elif cat == "Communication":
                # t = (name, start_us, end_us, deviceId, streamId, count, dataType, commName, algType, corrId)
                yield {
                    "name": str(t[0]),
                    "cat": "Communication",
                    "ph": "X",
                    "ts": start_us,
                    "dur": dur_us,
                    "pid": _device_pid(t[3], 0),
                    "tid": int(t[4]),
                    "args": {
                        "count": int(t[5]),
                        "dataType": str(t[6]),
                        "commName": str(t[7]),
                        "algType": str(t[8]),
                        "deviceId": int(t[3]),
                        "streamId": int(t[4]),
                        "correlationId": int(t[9]),
                    },
                }
            elif cat == "Marker":
                # t = (name, sourceKind, domain, id, start_us, end_us, pid, tid, deviceId, streamId)
                if str(t[1]) == "Host":
                    pid = int(t[6])
                    tid = int(t[7])
                else:
                    pid = _device_pid(t[8], 0)
                    tid = int(t[9])
                yield {
                    "name": str(t[0]),
                    "cat": "Marker",
                    "ph": "X",
                    "ts": start_us,
                    "dur": dur_us,
                    "pid": pid,
                    "tid": tid,
                    "args": {"sourceKind": str(t[1]), "domain": str(t[2]), "id": int(t[3])},
                }
            elif api_like:
                # t = (name, start_us, end_us, pid, tid, corrId)
                yield {
                    "name": str(t[0]),
                    "cat": cat,
                    "ph": "X",
                    "ts": start_us,
                    "dur": dur_us,
                    "pid": int(t[3]),
                    "tid": int(t[4]),
                    "args": {"correlationId": int(t[5])},
                }

    @classmethod
    def _dcmi_events(cls, items, base_ns, metric_meta):
        # DcmiSample.to_tuple() = (kind_int, ts_ns, deviceId, value)
        # counter 事件进入对应硬件层进程，曲线名 = displayName + " (" + unit + ")"。
        for row in items:
            t = row.to_tuple()
            kind_int = int(t[0])
            ts_us = (row.timestampNs - base_ns) / NS_TO_US
            meta = (metric_meta or {}).get(kind_int, {})
            layer = meta.get("layer", 0)
            order = LAYER_TO_ORDER.get(int(layer), 1)
            name = meta.get("displayName", f"metric{kind_int}")
            unit = meta.get("unit", "")
            if unit:
                name = f"{name} ({unit})"
            yield {
                "name": name,
                "cat": "DCMI",
                "ph": "C",
                "ts": ts_us,
                "pid": _device_pid(t[2], order),
                "tid": 0,
                "args": {"value": float(row.value)},
            }
