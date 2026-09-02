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

"""Chrome Trace 导出器单测：不依赖真实 C 扩展，用假数据行驱动。

运行：python -m unittest test_ut_python.test_chrome_trace_exporter
（或从 test/ut/python 目录 python -m unittest discover）
"""

import json
import os
import sys
import tempfile
import types
import unittest
from unittest import mock

# ---- 构造 IPCMonitor 包环境（复用真实 chrome_trace_exporter.py，仅替换文件路径） ----
_IPCMONITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "plugin", "IPCMonitor"))
sys.path.insert(0, os.path.dirname(_IPCMONITOR_DIR))

_pkg = types.ModuleType("IPCMonitor")
_pkg.__path__ = [_IPCMONITOR_DIR]
sys.modules["IPCMonitor"] = _pkg

from IPCMonitor import chrome_trace_exporter  # noqa: E402

# FileManager 依赖 os.getuid（Unix only），单测中跳过目录/权限检查，
# 导出器仍以 open() 直写临时目录文件。
_file_manager_patch = mock.patch(
    "IPCMonitor.chrome_trace_exporter.FileManager.create_file_by_path", side_effect=lambda p: None
)
_file_manager_patch.start()

PID_BASE = chrome_trace_exporter.PID_BASE
PPD = chrome_trace_exporter.PROCESSES_PER_DEVICE


class FakeActivityRow:
    """模拟 Kernel/Communication/API/Marker 行。"""

    def __init__(self, to_tuple, start_ns, end_ns):
        self._t = to_tuple
        self.startNs = start_ns
        self.endNs = end_ns

    def to_tuple(self):
        return self._t


class FakeDcmiRow:
    """模拟 DcmiSample。"""

    def __init__(self, kind_int, ts_ns, device_id, value):
        self.kind = kind_int
        self.timestampNs = ts_ns
        self.deviceId = device_id
        self.value = value

    def to_tuple(self):
        return (self.kind, self.timestampNs, self.deviceId, self.value)


# C++ 注册表元数据样例（内部 _get_dcmi_metric_meta() 的返回格式）
META = {
    0: {"kind": 0, "name": "Power", "layer": 0, "layerName": "DEVICE", "displayName": "Power", "unit": "W"},
    2: {
        "kind": 2,
        "name": "AICoreFreq",
        "layer": 1,
        "layerName": "AICORE",
        "displayName": "AICore Freq",
        "unit": "MHz",
    },
    10: {
        "kind": 10,
        "name": "AICPUUtil",
        "layer": 2,
        "layerName": "AICPU",
        "displayName": "AICPU Utilization",
        "unit": "%",
    },
    11: {"kind": 11, "name": "HBMFreq", "layer": 3, "layerName": "HBM", "displayName": "HBM Freq", "unit": "MHz"},
}


def _kernel_row(name, start_ns, end_ns, dev, stream, corr, ktype="AI Core"):
    t = (name, start_ns / 1000.0, end_ns / 1000.0, dev, stream, corr, ktype)
    return FakeActivityRow(t, start_ns, end_ns)


def _comm_row(name, start_ns, end_ns, dev, stream):
    # Communication.to_tuple = (name, start_us, end_us, deviceId, streamId, count, dataType, commName, algType, corrId)
    t = (name, start_ns / 1000.0, end_ns / 1000.0, dev, stream, 1024, "FP16", "hccl", "RING", 1)
    return FakeActivityRow(t, start_ns, end_ns)


class ChromeTraceExporterTest(unittest.TestCase):
    def _export(self, result, meta=None, status=None):
        fd, path = tempfile.mkstemp(suffix=".json")
        os.close(fd)
        os.unlink(path)
        chrome_trace_exporter.ChromeTraceExporter.export(path, result, meta or META, status)
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        os.unlink(path)
        return data

    def test_valid_schema(self):
        result = {
            "Kernel": [_kernel_row("k0", 1_000_000, 2_000_000, 0, 1, 7)],
        }
        data = self._export(result)
        self.assertIn("traceEvents", data)
        self.assertIn("metadata", data)
        self.assertIn("traceStartNs", data["metadata"])

    def test_kernel_event_pid_tid(self):
        result = {"Kernel": [_kernel_row("k0", 1_000_000, 2_000_000, 2, 3, 7)]}
        data = self._export(result)
        events = [e for e in data["traceEvents"] if e["ph"] == "X" and e["cat"] == "Kernel"]
        self.assertEqual(len(events), 1)
        ev = events[0]
        # Ascend Hardware 进程：pid = PID_BASE + dev*PPD + 0，stream 泳道：tid = streamId
        self.assertEqual(ev["pid"], PID_BASE + 2 * PPD + 0)
        self.assertEqual(ev["tid"], 3)
        self.assertEqual(ev["dur"], 1000.0)  # 1ms

    def test_dcmi_counter_enters_correct_layer(self):
        rows = [
            FakeDcmiRow(2, 1_000_000, 0, 800.0),  # AICoreFreq → AICore 进程(order=2)
            FakeDcmiRow(10, 2_000_000, 0, 55.0),  # AICPUUtil → AICPU 进程(order=3)
            FakeDcmiRow(11, 3_000_000, 0, 1600.0),  # HBMFreq → HBM 进程(order=4)
            FakeDcmiRow(0, 4_000_000, 0, 222.0),  # Power → Power/Temp 进程(order=1)
        ]
        data = self._export({"DCMI": rows})
        counters = [e for e in data["traceEvents"] if e["ph"] == "C"]
        self.assertEqual(len(counters), 4)
        by_name = {e["name"]: e for e in counters}
        self.assertEqual(by_name["Power (W)"]["pid"], PID_BASE + 0 * PPD + 1)
        self.assertEqual(by_name["AICore Freq (MHz)"]["pid"], PID_BASE + 0 * PPD + 2)
        self.assertEqual(by_name["AICPU Utilization (%)"]["pid"], PID_BASE + 0 * PPD + 3)
        self.assertEqual(by_name["HBM Freq (MHz)"]["pid"], PID_BASE + 0 * PPD + 4)
        self.assertEqual(by_name["AICore Freq (MHz)"]["args"]["value"], 800.0)

    def test_timestamp_relative_to_base(self):
        rows = [
            _kernel_row("k0", 5_000_000, 6_000_000, 0, 0, 1),
            FakeDcmiRow(2, 100_000_000, 0, 800.0),
        ]
        data = self._export({"Kernel": rows[:1], "DCMI": rows[1:]})
        x_events = [e for e in data["traceEvents"] if e["ph"] == "X" and e["cat"] == "Kernel"]
        c_events = [e for e in data["traceEvents"] if e["ph"] == "C"]
        self.assertEqual(x_events[0]["ts"], 0.0)  # 最小时间戳归零
        self.assertEqual(c_events[0]["ts"], (100_000_000 - 5_000_000) / 1000.0)
        self.assertEqual(data["metadata"]["traceStartNs"], 5_000_000)

    def test_process_metadata_names(self):
        rows = [_kernel_row("k0", 1_000_000, 2_000_000, 0, 2, 1)]
        data = self._export({"Kernel": rows})
        names = {e["args"]["name"] for e in data["traceEvents"] if e["name"] == "process_name"}
        self.assertIn("Device 0 · Ascend Hardware", names)
        thread_names = {e["args"]["name"] for e in data["traceEvents"] if e["name"] == "thread_name"}
        self.assertIn("Stream 2", thread_names)

    def test_dcmi_counter_pids_classified_as_device(self):
        # 回归：仅 DCMI（无 kernel）时，counter 所在层进程必须算设备进程（命名正确、不出现假 Host）
        rows = [
            FakeDcmiRow(2, 1_000_000, 0, 800.0),  # AICoreFreq → order=2
            FakeDcmiRow(11, 2_000_000, 0, 1600.0),
        ]  # HBMFreq → order=4
        data = self._export({"DCMI": rows})
        pnames = {e["pid"]: e["args"]["name"] for e in data["traceEvents"] if e["name"] == "process_name"}
        self.assertEqual(pnames.get(PID_BASE + 2), "Device 0 · AICore")
        self.assertEqual(pnames.get(PID_BASE + 4), "Device 0 · HBM")
        host_pids = [pid for pid, name in pnames.items() if name == "Host"]
        self.assertEqual(host_pids, [])

    def test_process_ordering_host_on_top(self):
        # 排序：Host(sort_index=0) 置顶，设备进程在其下；
        # 每设备 Ascend Hardware(order=0,泳道) → Power/Temp(1) → AICore(2) → ... → Overlap(5)
        rows = [_kernel_row("k0", 1_000_000, 2_000_000, 0, 0, 1)]
        host_marker = FakeActivityRow(
            ("host_marker", "Host", "default", 1, 1, 2, 2111616, 2112642, 0, 0), 1_000_000, 2_000_000
        )
        dcmi_row = FakeDcmiRow(2, 1_000_000, 0, 800.0)  # AICoreFreq → order=2
        power_row = FakeDcmiRow(0, 1_000_000, 0, 222.0)  # Power → order=1
        data = self._export({"Kernel": rows, "Marker": [host_marker], "DCMI": [dcmi_row, power_row]})
        idx = {e["pid"]: e["args"]["sort_index"] for e in data["traceEvents"] if e["name"] == "process_sort_index"}
        self.assertEqual(idx[2111616], 0)  # Host 置顶
        self.assertEqual(idx[PID_BASE + 0], 1000)  # Ascend Hardware
        self.assertLess(idx[PID_BASE + 0], idx[PID_BASE + 1])  # 泳道在 Power/Temp 上
        self.assertLess(idx[PID_BASE + 1], idx[PID_BASE + 2])  # Power/Temp 在 AICore 上

    def test_overlap_analysis_rows(self):
        # 计算/通信掩盖：kernel 与 comm 时间相交 → Overlapped；不相交 → Not Overlapped；间隙 → Free
        k = _kernel_row("k0", 10_000_000, 20_000_000, 0, 0, 1)  # [10,20]ms
        comm_over = _comm_row("ar0", 12_000_000, 14_000_000, 0, 0)  # 与 kernel 相交
        comm_free = _comm_row("ar1", 25_000_000, 27_000_000, 0, 0)  # 不与 kernel 相交
        data = self._export({"Kernel": [k], "Communication": [comm_over, comm_free]})
        oa_pid = PID_BASE + 0 * PPD + 5
        names = {e["name"] for e in data["traceEvents"] if e["ph"] == "X" and e["pid"] == oa_pid}
        self.assertIn("Computing", names)
        self.assertIn("Communication(Overlapped)", names)
        self.assertIn("Communication(Not Overlapped)", names)
        self.assertIn("Free", names)
        # Overlap Analysis 进程名与行名
        pnames = {e["pid"]: e["args"]["name"] for e in data["traceEvents"] if e["name"] == "process_name"}
        self.assertEqual(pnames.get(oa_pid), "Device 0 · Overlap Analysis")
        tnames = {
            e["tid"]: e["args"]["name"]
            for e in data["traceEvents"]
            if e["name"] == "thread_name" and e["pid"] == oa_pid
        }
        self.assertEqual(tnames.get(0), "Computing")
        self.assertEqual(tnames.get(2), "Communication(Not Overlapped)")
        # Free 区间：kernel 结束(20ms) 到 第二个 comm 开始(25ms)
        free_ev = [e for e in data["traceEvents"] if e["pid"] == oa_pid and e["name"] == "Free"]
        self.assertTrue(free_ev)
        self.assertAlmostEqual(free_ev[0]["ts"], 10_000.0)  # (20ms-10ms base)/1000? base=10ms
        self.assertAlmostEqual(free_ev[0]["dur"], 5_000.0)

    def test_dcmi_status_in_metadata(self):
        rows = [FakeDcmiRow(2, 1_000_000, 0, 800.0)]
        status = {"load": {"status": 0, "libPath": "/usr/lib64/libdcmi.so"}, "ringSize": 10}
        data = self._export({"DCMI": rows}, status=status)
        self.assertEqual(data["metadata"]["dcmi"]["load"]["status"], 0)
        self.assertEqual(data["metadata"]["dcmi"]["ringSize"], 10)

    def test_empty_result_no_crash(self):
        data = self._export({})
        self.assertEqual(data["traceEvents"], [])


if __name__ == "__main__":
    unittest.main()
