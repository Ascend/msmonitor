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

"""PyDynamicMonitorProxy 单测：假 C 扩展记录调用，不加载真实 IPCMonitor_C。

验证：init/poll/finalize/status 透传，NPU_MONITOR_STOP 为真时先写入通信组。
运行：python -m unittest test_ut_python.test_dynamic_monitor_proxy
"""

# 各用例文件各自构造 IPCMonitor 包，和已有 UT 一样，不抽公共模块。
# pylint: disable=duplicate-code

import os
import sys
import types
import unittest
from unittest import mock

# ---- 构造 IPCMonitor 包环境 ----
_IPCMONITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "plugin", "IPCMonitor"))
sys.path.insert(0, os.path.dirname(_IPCMONITOR_DIR))

_pkg = types.ModuleType("IPCMonitor")
_pkg.__path__ = [_IPCMONITOR_DIR]
sys.modules["IPCMonitor"] = _pkg

# ---- 假 C 扩展模块 ----
_calls = []


def _build_fake_c_module():
    fake = types.ModuleType("IPCMonitor._ipcmonitor_C")

    def init_dyno(npu_id):
        _calls.append(("init", npu_id))
        return 0

    def poll_dyno():
        _calls.append("poll")
        return {"samples": 1}

    def finalize_dyno():
        _calls.append("finalize")

    def update_profiler_status(status):
        _calls.append(("status", status))

    def set_cluster_config_data(data):
        _calls.append(("cluster", data))

    def enable_dyno_npu_monitor(config):
        _calls.append(("enable", config))

    fake.init_dyno = init_dyno
    fake.poll_dyno = poll_dyno
    fake.finalize_dyno = finalize_dyno
    fake.update_profiler_status = update_profiler_status
    fake.set_cluster_config_data = set_cluster_config_data
    fake.enable_dyno_npu_monitor = enable_dyno_npu_monitor
    fake.ipcmonitor_C_module = fake  # 与真实 _ipcmonitor_C.py 的导出名一致
    return fake


sys.modules["IPCMonitor._ipcmonitor_C"] = _build_fake_c_module()

from IPCMonitor.dynamic_monitor_proxy import PyDynamicMonitorProxy  # noqa: E402


class DynamicMonitorProxyTest(unittest.TestCase):
    def setUp(self):
        _calls.clear()

    def _proxy(self):
        # @Singleton 装饰器返回包装对象，调用得到单例实例
        return PyDynamicMonitorProxy()

    def test_dyno_calls_pass_through(self):
        proxy = self._proxy()
        self.assertEqual(proxy.init_dyno(2), 0)
        self.assertEqual(proxy.poll_dyno(), {"samples": 1})
        proxy.finalize_dyno()
        proxy.update_profiler_status({"on": True})
        self.assertEqual(_calls, [("init", 2), "poll", "finalize", ("status", {"on": True})])

    def test_enable_stop_records_parallel_group_before_monitor(self):
        with mock.patch("IPCMonitor.dynamic_monitor_proxy.get_parallel_group_info", return_value="group-json"):
            self._proxy().enable_dyno_npu_monitor({"NPU_MONITOR_STOP": "True"})
        self.assertEqual(_calls[0], ("cluster", {"parallel_group_info": "group-json"}))
        self.assertEqual(_calls[1], ("enable", {"NPU_MONITOR_STOP": "True"}))

    def test_enable_without_stop_skips_parallel_group(self):
        self._proxy().enable_dyno_npu_monitor({"NPU_MONITOR_STOP": "false"})
        self._proxy().enable_dyno_npu_monitor({})
        self.assertEqual(_calls, [("enable", {"NPU_MONITOR_STOP": "false"}), ("enable", {})])


if __name__ == "__main__":
    unittest.main()
