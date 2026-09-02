/*
 * Copyright (C) 2025-2025. Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ipc_monitor/PyDynamicMonitorProxy.h"
#include "ipc_monitor/dcmi/DcmiTypes.h"
#include "ipc_monitor/monitor/ActivityData.h"
#include "ipc_monitor/monitor/Monitor.h"
#include "ipc_monitor/mspti_monitor/MsptiMonitor.h"

namespace py = pybind11;

namespace
{
using dynolog_npu::ipc_monitor::monitor::DcmiApiStatus;
using dynolog_npu::ipc_monitor::monitor::DcmiLayer;
using dynolog_npu::ipc_monitor::monitor::DcmiMetricKind;
using dynolog_npu::ipc_monitor::monitor::DcmiSample;
using dynolog_npu::ipc_monitor::monitor::DcmiStatusSnapshot;

std::string LayerName(DcmiLayer layer)
{
    switch (layer)
    {
        case DcmiLayer::DEVICE:
            return "DEVICE";
        case DcmiLayer::AICORE:
            return "AICORE";
        case DcmiLayer::AICPU:
            return "AICPU";
        case DcmiLayer::HBM:
            return "HBM";
        default:
            return "UNKNOWN";
    }
}

py::dict ToDict(const DcmiStatusSnapshot& snap)
{
    py::dict load;
    load["status"] = static_cast<int32_t>(snap.load.status);
    load["statusName"] = snap.load.status == DcmiApiStatus::OK            ? "OK"
                         : snap.load.status == DcmiApiStatus::NOT_SUPPORT ? "NOT_SUPPORT"
                                                                          : "FAILED";
    load["version"] = snap.load.version;  // "V1" / "V2"（950 等 v2-only 代际）
    load["libPath"] = snap.load.libPath;
    load["missingSymbols"] = snap.load.missingSymbols;
    load["initRet"] = snap.load.initRet;
    load["cardNum"] = snap.load.cardNum;
    load["deviceNumPerCard"] = snap.load.deviceNumPerCard;
    load["devices"] = snap.load.devices;
    load["error"] = snap.load.error;

    py::list meta;
    for (const auto& m : snap.meta)
    {
        py::dict d;
        d["kind"] = static_cast<int32_t>(m.kind);
        d["name"] = m.name;
        d["layer"] = static_cast<int32_t>(m.layer);
        d["layerName"] = LayerName(m.layer);
        d["displayName"] = m.displayName;
        d["unit"] = m.unit;
        meta.append(d);
    }

    py::list kinds;
    for (const auto& entry : snap.kinds)
    {
        py::dict d;
        d["kind"] = static_cast<int32_t>(entry.first);
        d["enabled"] = entry.second.enabled;
        d["reason"] = entry.second.reason;
        kinds.append(d);
    }

    py::list groupCalls;
    for (const auto& entry : snap.groupCallCounts)
    {
        groupCalls.append(py::make_tuple(entry.first, entry.second));
    }
    py::list groupNs;
    for (const auto& entry : snap.groupCollectNs)
    {
        groupNs.append(py::make_tuple(entry.first, entry.second));
    }
    py::list groupFails;
    for (const auto& entry : snap.groupFailCounts)
    {
        groupFails.append(py::make_tuple(entry.first, entry.second));
    }
    py::list lastCodes;
    for (const auto& entry : snap.lastErrorCodes)
    {
        lastCodes.append(py::make_tuple(entry.first, entry.second));
    }

    py::dict out;
    out["load"] = load;
    out["metricMeta"] = meta;
    out["kinds"] = kinds;
    out["tickCount"] = snap.tickCount;
    out["overrunCount"] = snap.overrunCount;
    out["lastTickCollectUs"] = snap.lastTickCollectUs;
    out["groupCallCounts"] = groupCalls;
    out["groupCollectNs"] = groupNs;
    out["ringDropCount"] = snap.ringDropCount;
    out["ringSize"] = snap.ringSize;
    out["ringCapacity"] = snap.ringCapacity;
    out["groupFailCounts"] = groupFails;
    out["lastErrorCodes"] = lastCodes;
    return out;
}
}  // namespace

void init_monitor_module(py::module& m)
{
    using namespace dynolog_npu::ipc_monitor::monitor;
    auto monitor_m = m.def_submodule("monitor");
    py::enum_<msptiActivityKind>(monitor_m, "ActivityKind")
        .value("API", msptiActivityKind::MSPTI_ACTIVITY_KIND_API)
        .value("Kernel", msptiActivityKind::MSPTI_ACTIVITY_KIND_KERNEL)
        .value("Communication", msptiActivityKind::MSPTI_ACTIVITY_KIND_COMMUNICATION)
        .value("Marker", msptiActivityKind::MSPTI_ACTIVITY_KIND_MARKER)
        .value("AclAPI", msptiActivityKind::MSPTI_ACTIVITY_KIND_ACL_API)
        .value("NodeAPI", msptiActivityKind::MSPTI_ACTIVITY_KIND_NODE_API)
        .value("RuntimeAPI", msptiActivityKind::MSPTI_ACTIVITY_KIND_RUNTIME_API);
    // 硬件层（DCMI 采集主配置单位）
    py::enum_<DcmiLayer>(monitor_m, "DcmiLayer")
        .value("DEVICE", DcmiLayer::DEVICE)
        .value("AICORE", DcmiLayer::AICORE)
        .value("AICPU", DcmiLayer::AICPU)
        .value("HBM", DcmiLayer::HBM);
    // 层内细粒度指标（高级精调）
    py::enum_<DcmiMetricKind>(monitor_m, "DcmiMetricKind")
        .value("Power", DcmiMetricKind::Power)
        .value("Temp", DcmiMetricKind::Temp)
        .value("AICoreFreq", DcmiMetricKind::AICoreFreq)
        .value("AICoreRatedFreq", DcmiMetricKind::AICoreRatedFreq)
        .value("AICoreUtil", DcmiMetricKind::AICoreUtil)
        .value("AICubeUtil", DcmiMetricKind::AICubeUtil)
        .value("VectorCoreUtil", DcmiMetricKind::VectorCoreUtil)
        .value("NPUUtil", DcmiMetricKind::NPUUtil)
        .value("AICPUMaxFreq", DcmiMetricKind::AICPUMaxFreq)
        .value("AICPUFreq", DcmiMetricKind::AICPUFreq)
        .value("AICPUUtil", DcmiMetricKind::AICPUUtil)
        .value("HBMFreq", DcmiMetricKind::HBMFreq)
        .value("HBMMemUsed", DcmiMetricKind::HBMMemUsed)
        .value("HBMMemTotal", DcmiMetricKind::HBMMemTotal)
        .value("HBMBandwidth", DcmiMetricKind::HBMBandwidth)
        .value("HBMTemp", DcmiMetricKind::HBMTemp)
        .value("Voltage", DcmiMetricKind::Voltage);
    py::class_<API>(monitor_m, "API")
        .def(py::init<>())
        .def_readwrite("name", &API::name)
        .def_readwrite("startNs", &API::startNs)
        .def_readwrite("endNs", &API::endNs)
        .def_readwrite("pid", &API::pid)
        .def_readwrite("tid", &API::tid)
        .def_readwrite("correlationId", &API::correlationId)
        .def("to_tuple", &API::to_tuple);
    py::class_<Kernel>(monitor_m, "Kernel")
        .def(py::init<>())
        .def_readwrite("name", &Kernel::name)
        .def_readwrite("startNs", &Kernel::startNs)
        .def_readwrite("endNs", &Kernel::endNs)
        .def_readwrite("deviceId", &Kernel::deviceId)
        .def_readwrite("streamId", &Kernel::streamId)
        .def_readwrite("correlationId", &Kernel::correlationId)
        .def_readwrite("type", &Kernel::type)
        .def("to_tuple", &Kernel::to_tuple);
    py::class_<Communication>(monitor_m, "Communication")
        .def(py::init<>())
        .def_readwrite("name", &Communication::name)
        .def_readwrite("startNs", &Communication::startNs)
        .def_readwrite("endNs", &Communication::endNs)
        .def_readwrite("deviceId", &Communication::deviceId)
        .def_readwrite("streamId", &Communication::streamId)
        .def_readwrite("count", &Communication::count)
        .def_readwrite("dataType", &Communication::dataType)
        .def_readwrite("commName", &Communication::commName)
        .def_readwrite("algType", &Communication::algType)
        .def_readwrite("correlationId", &Communication::correlationId)
        .def("to_tuple", &Communication::to_tuple);
    py::class_<Marker>(monitor_m, "Marker")
        .def(py::init<>())
        .def_readwrite("name", &Marker::name)
        .def_readwrite("sourceKind", &Marker::sourceKind)
        .def_readwrite("domain", &Marker::domain)
        .def_readwrite("id", &Marker::id)
        .def_readwrite("startNs", &Marker::startNs)
        .def_readwrite("endNs", &Marker::endNs)
        .def_readwrite("pid", &Marker::pid)
        .def_readwrite("tid", &Marker::tid)
        .def_readwrite("deviceId", &Marker::deviceId)
        .def_readwrite("streamId", &Marker::streamId)
        .def("to_tuple", &Marker::to_tuple);
    py::class_<DcmiSample>(monitor_m, "DcmiSample")
        .def(py::init<>())
        .def_readwrite("kind", &DcmiSample::kind)
        .def_readwrite("timestampNs", &DcmiSample::timestampNs)
        .def_readwrite("deviceId", &DcmiSample::deviceId)
        .def_readwrite("value", &DcmiSample::value)
        .def("to_tuple", &DcmiSample::to_tuple);

    monitor_m.def(
        "start_monitor",
        [](const std::vector<msptiActivityKind>& kinds, const std::vector<DcmiLayer>& dcmiLayers,
           const std::vector<DcmiMetricKind>& dcmiMetrics, uint32_t dcmiIntervalMs,
           const std::vector<uint32_t>& devices) -> void
        {
            std::set<DcmiLayer> layerSet(dcmiLayers.begin(), dcmiLayers.end());
            std::set<DcmiMetricKind> metricSet(dcmiMetrics.begin(), dcmiMetrics.end());
            Monitor::GetInstance()->Start(kinds, layerSet, metricSet, dcmiIntervalMs, devices);
        },
        py::arg("kinds"), py::arg("dcmi_layers") = std::vector<DcmiLayer>(),
        py::arg("dcmi_metrics") = std::vector<DcmiMetricKind>(),
        py::arg("dcmi_interval_ms") = static_cast<uint32_t>(10), py::arg("devices") = std::vector<uint32_t>());
    monitor_m.def("stop_monitor", []() -> void { Monitor::GetInstance()->Stop(); });
    monitor_m.def("get_kinds", []() { return Monitor::GetInstance()->GetKinds(); });
    monitor_m.def("get_api_data", []() { return Monitor::GetInstance()->GetAPIData(); }, py::return_value_policy::move);
    monitor_m.def(
        "get_acl_api_data", []() { return Monitor::GetInstance()->GetAclApiData(); }, py::return_value_policy::move);
    monitor_m.def(
        "get_node_api_data", []() { return Monitor::GetInstance()->GetNodeApiData(); }, py::return_value_policy::move);
    monitor_m.def(
        "get_runtime_api_data", []() { return Monitor::GetInstance()->GetRuntimeApiData(); },
        py::return_value_policy::move);
    monitor_m.def(
        "get_kernel_data", []() { return Monitor::GetInstance()->GetKernelData(); }, py::return_value_policy::move);
    monitor_m.def(
        "get_communication_data", []() { return Monitor::GetInstance()->GetCommunicationData(); },
        py::return_value_policy::move);
    monitor_m.def(
        "get_marker_data", []() { return Monitor::GetInstance()->GetMarkerData(); }, py::return_value_policy::move);
    monitor_m.def(
        "get_dcmi_data", []() { return Monitor::GetInstance()->GetDcmiData(); }, py::return_value_policy::move);
    monitor_m.def("get_dcmi_status", []() -> py::dict { return ToDict(Monitor::GetInstance()->GetDcmiStatus()); });
    monitor_m.def("get_dcmi_metric_meta",
                  []() -> py::list
                  {
                      py::list out;
                      for (const auto& m : Monitor::GetInstance()->GetDcmiMetricMeta())
                      {
                          py::dict d;
                          d["kind"] = static_cast<int32_t>(m.kind);
                          d["name"] = m.name;
                          d["layer"] = static_cast<int32_t>(m.layer);
                          d["layerName"] = LayerName(m.layer);
                          d["displayName"] = m.displayName;
                          d["unit"] = m.unit;
                          out.append(d);
                      }
                      return out;
                  });
}

PYBIND11_MODULE(IPCMonitor_C, m)
{
    m.def(
        "init_dyno", [](int npu_id) -> bool
        { return dynolog_npu::ipc_monitor::PyDynamicMonitorProxy::GetInstance()->InitDyno(npu_id); },
        py::arg("npu_id"));
    m.def("poll_dyno",
          []() -> std::string { return dynolog_npu::ipc_monitor::PyDynamicMonitorProxy::GetInstance()->PollDyno(); });
    m.def(
        "enable_dyno_npu_monitor", [](std::unordered_map<std::string, std::string>& config_map) -> void
        { dynolog_npu::ipc_monitor::PyDynamicMonitorProxy::GetInstance()->EnableMsptiMonitor(config_map); },
        py::arg("config_map"));
    m.def("finalize_dyno",
          []() -> void { dynolog_npu::ipc_monitor::PyDynamicMonitorProxy::GetInstance()->FinalizeDyno(); });
    m.def(
        "set_cluster_config_data", [](const std::unordered_map<std::string, std::string>& cluster_config) -> void
        { dynolog_npu::ipc_monitor::MsptiMonitor::GetInstance()->SetClusterConfigData(cluster_config); },
        py::arg("cluster_config"));
    m.def(
        "update_profiler_status", [](std::unordered_map<std::string, std::string>& status) -> void
        { dynolog_npu::ipc_monitor::PyDynamicMonitorProxy::GetInstance()->UpdateProfilerStatus(status); },
        py::arg("status"));

    init_monitor_module(m);
}
