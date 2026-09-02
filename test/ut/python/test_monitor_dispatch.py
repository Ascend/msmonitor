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

"""Monitor Python API 单测：用假 _ipcmonitor_C 模块驱动，不依赖真实 C 扩展。

验证：start() 校验与参数透传（无 devices 参数）、get_result() 合并 DCMI、
save() 生成 msmonitor_<pid>_<时间戳> 文件夹并导出 Excel + Chrome Trace + log。
运行：python -m unittest test_ut_python.test_monitor_dispatch
"""

# Monitor 经 @Singleton 装饰为包装对象，测试经 Monitor._cls 访问底层类（pylint 静态分析不识别）
# pylint: disable=no-member

import enum
import os
import sys
import tempfile
import types
import unittest
from unittest import mock

_IPCMONITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "plugin", "IPCMonitor"))
sys.path.insert(0, os.path.dirname(_IPCMONITOR_DIR))

# ---- 假 C 扩展模块 ----
_calls = []  # start_monitor 调用记录


class _ActivityKind(enum.IntEnum):
    API = 0
    Kernel = 1
    Communication = 2
    Marker = 3
    AclAPI = 4
    NodeAPI = 5
    RuntimeAPI = 6


class _DcmiLayer(enum.IntEnum):
    DEVICE = 0
    AICORE = 1
    AICPU = 2
    HBM = 3


class _DcmiMetricKind(enum.IntEnum):
    Power = 0
    Temp = 1
    AICoreFreq = 2
    AICoreRatedFreq = 3
    AICoreUtil = 4
    AICubeUtil = 5
    VectorCoreUtil = 6
    NPUUtil = 7
    AICPUMaxFreq = 8
    AICPUFreq = 9
    AICPUUtil = 10
    HBMFreq = 11
    HBMMemUsed = 12
    HBMMemTotal = 13
    HBMBandwidth = 14
    HBMTemp = 15
    Voltage = 16


_META = [
    {"kind": 0, "name": "Power", "layer": 0, "layerName": "DEVICE", "displayName": "Power", "unit": "W"},
    {"kind": 1, "name": "Temp", "layer": 0, "layerName": "DEVICE", "displayName": "Temperature", "unit": "\u2103"},
    {"kind": 2, "name": "AICoreFreq", "layer": 1, "layerName": "AICORE", "displayName": "AICore Freq", "unit": "MHz"},
    {
        "kind": 8,
        "name": "AICPUMaxFreq",
        "layer": 2,
        "layerName": "AICPU",
        "displayName": "AICPU Max Freq",
        "unit": "MHz",
    },
    {"kind": 11, "name": "HBMFreq", "layer": 3, "layerName": "HBM", "displayName": "HBM Freq", "unit": "MHz"},
]


class _FakeSample:
    def __init__(self, kind, ts_ns, dev, value):
        self.kind = kind
        self.timestampNs = ts_ns
        self.deviceId = dev
        self.value = value

    def to_tuple(self):
        return (int(self.kind), self.timestampNs, self.deviceId, self.value)


def _build_fake_c_module():
    mon = types.ModuleType("monitor")
    mon.ActivityKind = _ActivityKind
    mon.DcmiLayer = _DcmiLayer
    mon.DcmiMetricKind = _DcmiMetricKind
    mon.start_monitor = lambda *a, **kw: _calls.append((a, kw))
    mon.stop_monitor = lambda: None
    mon.get_kinds = lambda: [_ActivityKind.Kernel]
    mon.get_api_data = list
    mon.get_acl_api_data = list
    mon.get_node_api_data = list
    mon.get_runtime_api_data = list
    mon.get_kernel_data = list
    mon.get_communication_data = list
    mon.get_marker_data = list
    mon.get_dcmi_data = lambda: [_FakeSample(_DcmiMetricKind.AICoreFreq, 2, 0, 800.0)]
    mon.get_dcmi_status = lambda: {"load": {"status": 0}, "ringSize": 1}
    mon.get_dcmi_metric_meta = lambda: list(_META)

    fake_c = types.ModuleType("_ipcmonitor_C")
    fake_c.monitor = mon
    fake_c.ipcmonitor_C_module = fake_c  # 与真实 _ipcmonitor_C.py 的导出名一致
    return fake_c


_pkg = types.ModuleType("IPCMonitor")
_pkg.__path__ = [_IPCMONITOR_DIR]
sys.modules["IPCMonitor"] = _pkg
sys.modules["IPCMonitor._ipcmonitor_C"] = _build_fake_c_module()

from IPCMonitor.monitor import ActivityKind, DcmiLayer, Monitor  # noqa: E402


class MonitorDispatchTest(unittest.TestCase):
    def setUp(self):
        _calls.clear()
        Monitor._cls._session_dir = None  # 每次用例重置会话目录
        self._old_env = dict(os.environ)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._old_env)  # 恢复 start() 设置的 MSMONITOR_LOG_PATH

    def _monitor(self):
        # @Singleton 装饰器返回包装对象，Monitor() 得到单例实例
        return Monitor()

    def test_start_valid_args_passthrough(self):
        self._monitor().start(
            kinds=[ActivityKind.Kernel],
            dcmi_layers=[DcmiLayer.AICORE, DcmiLayer.HBM],
            dcmi_interval_ms=10,
            save_path=tempfile.mkdtemp(),
        )
        self.assertEqual(len(_calls), 1)
        args, kwargs = _calls[0]
        self.assertEqual(args[0], [ActivityKind.Kernel])
        self.assertEqual(args[1], [DcmiLayer.AICORE, DcmiLayer.HBM])
        self.assertEqual(args[2], [])  # 指标级精调不再对外，恒传空
        self.assertEqual(args[3], 10)
        self.assertEqual(len(args), 4)  # devices 参数已移除，C++ 侧默认空（自动取当前 device）

    def test_start_defaults(self):
        self._monitor().start(kinds=[ActivityKind.Kernel])
        args, kwargs = _calls[0]
        self.assertEqual(args[1], [])  # dcmi_layers 缺省为空
        self.assertEqual(args[3], 10)  # dcmi_interval_ms 缺省 10ms
        # save_path 缺省为 None：不建会话目录、不改日志路径，仅在线获取数据
        self.assertIsNone(Monitor._cls._session_dir)
        self.assertNotIn("MSMONITOR_LOG_PATH", os.environ)

    def test_start_no_save_path_online_only(self):
        # save_path=None（默认）：仅在线 get_result()，不创建会话目录
        self._monitor().start(kinds=[ActivityKind.Kernel])
        self.assertIsNone(Monitor._cls._session_dir)
        self.assertNotIn("MSMONITOR_LOG_PATH", os.environ)
        self.assertEqual(len(_calls), 1)  # start_monitor 正常调用

    def test_start_invalid_save_path_rejected(self):
        self._monitor().start(kinds=[ActivityKind.Kernel], save_path=123)
        self.assertEqual(_calls, [])

    def test_start_invalid_kinds_rejected(self):
        self._monitor().start(kinds=["Kernel"])
        self.assertEqual(_calls, [])

    def test_start_invalid_layer_rejected(self):
        self._monitor().start(dcmi_layers=["AICORE"])
        self.assertEqual(_calls, [])

    def test_start_invalid_interval_rejected(self):
        self._monitor().start(kinds=[ActivityKind.Kernel], dcmi_interval_ms=0)
        self.assertEqual(_calls, [])

    def test_start_nothing_rejected(self):
        self._monitor().start()
        self.assertEqual(_calls, [])

    def test_get_result_merges_dcmi(self):
        result = self._monitor().get_result()
        # mspti kinds（Kernel）与 DCMI 样本都在结果里
        self.assertIn(ActivityKind.Kernel, result)
        dcmi_key = [k for k in result if getattr(k, "name", "") == "DCMI"]
        self.assertEqual(len(dcmi_key), 1)
        self.assertEqual(len(result[dcmi_key[0]]), 1)

    def test_get_dcmi_status_private(self):
        # get_dcmi_status 已收敛为私有 _get_dcmi_status（DFX 内部用）
        self.assertFalse(hasattr(Monitor._cls, "get_dcmi_status"))
        self.assertIn("load", self._monitor()._get_dcmi_status())

    def test_get_dcmi_meta_private(self):
        # get_dcmi_metric_meta / get_dcmi_layers 已收敛：元数据走私有接口，层清单接口删除
        self.assertFalse(hasattr(Monitor._cls, "get_dcmi_metric_meta"))
        self.assertFalse(hasattr(Monitor._cls, "get_dcmi_layers"))
        self.assertEqual(len(self._monitor()._get_dcmi_metric_meta()), len(_META))

    def test_save_writes_into_session_dir(self):
        # save() 无参：数据写入 start(save_path) 确定的会话目录，无移动/复制动作
        save_root = tempfile.mkdtemp()
        self._monitor().start(kinds=[ActivityKind.Kernel], save_path=save_root)
        session = Monitor._cls._session_dir
        self.assertTrue(os.path.isdir(session))  # 会话目录直接建在 save_path 下
        os.makedirs(os.path.join(session, "log"))  # 模拟 glog 已写入 log/
        result = {"DCMI": [_FakeSample(_DcmiMetricKind.AICoreFreq, 2, 0, 800.0)]}
        with (
            mock.patch.object(Monitor._cls, "get_result", return_value=result),
            mock.patch.object(Monitor._cls, "_save_excel") as save_excel,
            mock.patch.object(Monitor._cls, "_save_chrome_trace") as save_trace,
            mock.patch.object(Monitor._cls, "_cleanup_log_symlinks") as cleanup,
        ):
            self._monitor().save()
        self.assertTrue(os.path.isdir(session))  # 原地导出，目录未移动
        self.assertTrue(os.path.isdir(os.path.join(session, "log")))
        self.assertEqual(save_excel.call_args[0][0], os.path.join(session, "monitor_result.xlsx"))
        self.assertEqual(save_trace.call_args[0][0], os.path.join(session, "monitor_result.json"))
        cleanup.assert_called_once_with(session)  # 导出后清理 glog 符号链接

    def test_start_sets_session_dir_and_log_env(self):
        # start(save_path) 确定会话目录并把 glog 指向其 log/
        save_root = tempfile.mkdtemp()
        self._monitor().start(kinds=[ActivityKind.Kernel], save_path=save_root)
        session = Monitor._cls._session_dir
        self.assertTrue(session is not None)
        self.assertEqual(os.path.dirname(session), os.path.abspath(save_root))
        self.assertTrue(os.path.basename(session).startswith("msmonitor_%d_" % os.getpid()))
        self.assertEqual(os.environ.get("MSMONITOR_LOG_PATH"), os.path.join(session, "log"))

    def test_save_without_start_no_export(self):
        with mock.patch.object(Monitor._cls, "get_result") as get_result:
            self._monitor().save()
            get_result.assert_not_called()  # 无会话目录直接提示，不导出

    def test_save_nothing_no_export(self):
        self._monitor().start(kinds=[ActivityKind.Kernel], save_path=tempfile.mkdtemp())
        with (
            mock.patch.object(Monitor._cls, "get_result", return_value={}),
            mock.patch.object(Monitor._cls, "_save_excel") as save_excel,
            mock.patch.object(Monitor._cls, "_save_chrome_trace") as save_trace,
        ):
            self._monitor().save()
            save_excel.assert_not_called()
            save_trace.assert_not_called()


if __name__ == "__main__":
    unittest.main()
