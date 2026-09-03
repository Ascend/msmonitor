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

import os
from datetime import datetime

import xlsxwriter

from ._ipcmonitor_C import ipcmonitor_C_module
from .chrome_trace_exporter import ChromeTraceExporter
from .file_manager import FileManager
from .singleton import Singleton

ActivityKind = ipcmonitor_C_module.monitor.ActivityKind
DcmiLayer = ipcmonitor_C_module.monitor.DcmiLayer


class _DcmiSentinel:
    """DCMI 样本列表在 result dict 中的 key（跨全部指标/设备的扁平样本列表）。"""

    name = "DCMI"


DCMI = _DcmiSentinel()


@Singleton
class Monitor:
    API_HEADER = ["Name", "Start(us)", "End(us)", "Pid", "Tid", "Correlation ID", "Duration(us)"]

    HEADER = {
        ActivityKind.API: API_HEADER,
        ActivityKind.AclAPI: API_HEADER,
        ActivityKind.NodeAPI: API_HEADER,
        ActivityKind.RuntimeAPI: API_HEADER,
        ActivityKind.Kernel: [
            "Name",
            "Start(us)",
            "End(us)",
            "Device ID",
            "Stream ID",
            "Correlation ID",
            "Type",
            "Duration(us)",
        ],
        ActivityKind.Communication: [
            "Name",
            "Start(us)",
            "End(us)",
            "Device ID",
            "Stream ID",
            "Count",
            "DataType",
            "CommName",
            "AlgType",
            "Correlation ID",
            "Duration(us)",
        ],
        ActivityKind.Marker: [
            "Name",
            "SourceKind",
            "Domain",
            "ID",
            "Start(us)",
            "End(us)",
            "Pid",
            "Tid",
            "Device ID",
            "Stream ID",
            "Duration(us)",
        ],
        DCMI: ["Layer", "Metric", "Unit", "Timestamp(us)", "Device ID", "Value"],
    }

    GET_DATA_FUNC = {
        ActivityKind.API: ipcmonitor_C_module.monitor.get_api_data,
        ActivityKind.AclAPI: ipcmonitor_C_module.monitor.get_acl_api_data,
        ActivityKind.NodeAPI: ipcmonitor_C_module.monitor.get_node_api_data,
        ActivityKind.RuntimeAPI: ipcmonitor_C_module.monitor.get_runtime_api_data,
        ActivityKind.Kernel: ipcmonitor_C_module.monitor.get_kernel_data,
        ActivityKind.Communication: ipcmonitor_C_module.monitor.get_communication_data,
        ActivityKind.Marker: ipcmonitor_C_module.monitor.get_marker_data,
    }

    NS_TO_US = 1000.0
    _session_dir = None  # start(save_path) 创建的会话目录（glog 直接写入其 log/），save() 直接使用

    @classmethod
    def start(cls, kinds=None, dcmi_layers=None, dcmi_interval_ms=10, save_path=None):
        """统一配置采集项。

        Args:
            kinds: mspti 活动类型列表，如 [ActivityKind.Kernel]。
            dcmi_layers: DCMI 按硬件层配置，如 [DcmiLayer.AICORE, DcmiLayer.HBM]；
                选择某层即采集该层全部指标，空则不采集 DCMI。
            dcmi_interval_ms: DCMI 采样间隔，默认 10ms。
            save_path: 结果保存目录；为 None（默认）时不创建会话目录、不改日志落盘位置，
                仅通过 get_result() 在线获取数据；指定时创建 <save_path>/msmonitor_<pid>_<时间戳>/
                会话目录，glog 从一开始就直接写入其 log/，save() 无需再传路径。

        DCMI 采集设备无需指定：默认取当前进程已 set 的 device。
        """
        kinds = kinds or []
        dcmi_layers = dcmi_layers or []
        if not isinstance(kinds, list) or not all(isinstance(k, ActivityKind) for k in kinds):
            print("[ERROR] Invalid activity kind list")
            return
        if not isinstance(dcmi_layers, list) or not all(isinstance(layer, DcmiLayer) for layer in dcmi_layers):
            print("[ERROR] Invalid dcmi layer list")
            return
        if not isinstance(dcmi_interval_ms, int) or dcmi_interval_ms <= 0:
            print("[ERROR] Invalid dcmi interval ms")
            return
        if save_path is not None and not isinstance(save_path, str):
            print("[ERROR] Invalid save path")
            return
        if not kinds and not dcmi_layers:
            print("[ERROR] No activity kind or dcmi layer provided")
            return
        if save_path is not None:
            # 会话目录直接建在 save_path 下，路径一开始就确定；
            # glog 初始化（start_monitor 内）按 MSMONITOR_LOG_PATH 创建 log/ 并写入
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            session_dir = os.path.abspath(os.path.join(save_path, f"msmonitor_{os.getpid()}_{ts}"))
            if os.path.exists(session_dir):
                session_dir += "_" + datetime.now().strftime("%f")
            FileManager.make_dir_safety(session_dir)
            cls._session_dir = session_dir
            os.environ["MSMONITOR_LOG_PATH"] = os.path.join(session_dir, "log")
        else:
            # 仅在线获取数据：不建会话目录、不改日志路径（glog 走默认落盘位置）
            cls._session_dir = None
        ipcmonitor_C_module.monitor.start_monitor(kinds, dcmi_layers, [], dcmi_interval_ms)

    @classmethod
    def stop(cls):
        ipcmonitor_C_module.monitor.stop_monitor()

    @classmethod
    def get_result(cls) -> dict:
        result = {}
        kinds = ipcmonitor_C_module.monitor.get_kinds()
        for kind in kinds:
            if kind not in cls.GET_DATA_FUNC:
                print(f"[WARNING] Unsupported activity kind: {kind}")
                continue
            result[kind] = cls.GET_DATA_FUNC[kind]()

        dcmi_data = ipcmonitor_C_module.monitor.get_dcmi_data()
        if dcmi_data:
            result[DCMI] = dcmi_data
        return result

    @classmethod
    def _get_dcmi_status(cls) -> dict:
        """内部 DFX 快照（trace metadata 用），不对外暴露。"""
        try:
            return ipcmonitor_C_module.monitor.get_dcmi_status()
        except Exception as err:
            print(f"[ERROR] Failed to get dcmi status: {err}")
            return {}

    @classmethod
    def _get_dcmi_metric_meta(cls) -> list:
        """内部指标元数据（Excel/Chrome Trace 导出用），不对外暴露。"""
        return ipcmonitor_C_module.monitor.get_dcmi_metric_meta()

    @classmethod
    def save(cls):
        """导出全部已采集数据到 start(save_path=...) 确定的会话目录。

        数据（monitor_result.xlsx + monitor_result.json）与运行日志（log/ 子目录，
        glog 自 start 起直接写入）一并保存在其中；各文件只包含实际采集到的数据
        （无 DCMI 则无 DCMI Sheet/曲线，无算子则无对应 Sheet/事件）。
        """
        out_dir = cls._session_dir
        if not out_dir:
            print("[ERROR] No session dir: call start(save_path=...) first")
            return

        result = cls.get_result()
        if not result:
            print("[WARNING] No valid activity data")
            return

        FileManager.make_dir_safety(out_dir)
        base = os.path.join(out_dir, "monitor_result")
        cls._save_excel(base + ".xlsx", result)
        cls._save_chrome_trace(base + ".json", result)
        cls._cleanup_log_symlinks(out_dir)
        print(f"[INFO] Output dir: {out_dir}")

    @staticmethod
    def _cleanup_log_symlinks(out_dir: str):
        """删除 log/ 下 glog 生成的 MsMonitor.INFO 等符号链接（仅指向最新日志，非日志内容）。

        C++ 侧已通过 FLAGS_log_link="" 从源头禁止，此为旧二进制/异常路径的兜底。
        """
        log_dir = os.path.join(out_dir, "log")
        if not os.path.isdir(log_dir):
            return
        for name in os.listdir(log_dir):
            path = os.path.join(log_dir, name)
            if os.path.islink(path):
                try:
                    os.remove(path)
                except OSError:
                    pass

    @classmethod
    def _save_excel(cls, file_path: str, result: dict):
        if os.path.exists(file_path):
            print(f"[WARNING] File already exists: {file_path}, will be overwritten")
        try:
            print(f"[INFO] Start to save activity data file: {file_path}")
            meta_map = {int(m["kind"]): m for m in cls._get_dcmi_metric_meta()}

            FileManager.create_file_by_path(file_path)
            with xlsxwriter.Workbook(file_path) as workbook:
                for kind, data in result.items():
                    worksheet = workbook.add_worksheet(kind.name)
                    worksheet.write_row(0, 0, cls.HEADER[kind])
                    if kind is DCMI:
                        # 按 (Layer, Metric, Timestamp) 排序成连续块，保证各层/各指标完整性一目了然
                        dcm_rows = []
                        for row in data:
                            meta = meta_map.get(int(row.kind), {})
                            dcm_rows.append(
                                [
                                    meta.get("layerName", ""),
                                    meta.get("displayName", str(row.kind)),
                                    meta.get("unit", ""),
                                    row.timestampNs / cls.NS_TO_US,
                                    row.deviceId,
                                    row.value,
                                    meta.get("layerName", ""),
                                    meta.get("displayName", ""),
                                ]
                            )
                        dcm_rows.sort(key=lambda r: (r[6], r[7], r[3]))
                        for i, r in enumerate(dcm_rows, start=1):
                            worksheet.write_row(i, 0, r[:6])
                    else:
                        for i, row in enumerate(data, start=1):
                            worksheet.write_row(
                                i,
                                0,
                                [str(item) for item in row.to_tuple()] + [(row.endNs - row.startNs) / cls.NS_TO_US],
                            )

            print(f"[INFO] File saved successfully: {file_path}")
        except Exception as err:
            print(f"[ERROR] Failed to save file: {file_path}, error: {err}")

    @classmethod
    def _save_chrome_trace(cls, file_path: str, result: dict):
        try:
            print(f"[INFO] Start to save chrome trace file: {file_path}")
            meta_map = {int(m["kind"]): m for m in cls._get_dcmi_metric_meta()}
            dcmi_status = cls._get_dcmi_status()
            ChromeTraceExporter.export(file_path, result, meta_map, dcmi_status)
        except Exception as err:
            print(f"[ERROR] Failed to save chrome trace: {file_path}, error: {err}")
