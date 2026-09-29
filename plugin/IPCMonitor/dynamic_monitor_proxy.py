# -------------------------------------------------------------------------
# This file is part of the MindStudio project.
# Copyright (c) 2025 Huawei Technologies Co.,Ltd.
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

from ._ipcmonitor_C import ipcmonitor_C_module
from .singleton import Singleton
from .utils import get_parallel_group_info


@Singleton
class PyDynamicMonitorProxy:
    @classmethod
    def init_dyno(cls, npu_id: int):
        return ipcmonitor_C_module.init_dyno(npu_id)

    @classmethod
    def poll_dyno(cls):
        return ipcmonitor_C_module.poll_dyno()

    @classmethod
    def enable_dyno_npu_monitor(cls, config_map: dict):
        if str(config_map.get("NPU_MONITOR_STOP")).lower() in ("true", "1"):
            ipcmonitor_C_module.set_cluster_config_data({"parallel_group_info": get_parallel_group_info()})
        ipcmonitor_C_module.enable_dyno_npu_monitor(config_map)

    @classmethod
    def finalize_dyno(cls):
        ipcmonitor_C_module.finalize_dyno()

    @classmethod
    def update_profiler_status(cls, status: dict):
        ipcmonitor_C_module.update_profiler_status(status)
