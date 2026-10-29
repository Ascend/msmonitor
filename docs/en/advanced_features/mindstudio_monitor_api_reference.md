# mindstudio_monitor Module API Reference

## mindstudio_monitor Module Overview

The module provides IPC (Inter-Process Communication) interfaces and the capability to control msPTI Monitor independently to collect and obtain profile data.

1. IPC control channel: The profiler backend obtains the profiler configuration from the dynolog daemon.
2. IPC data channel: msPTI Monitor sends profile data to the dynolog daemon.
3. Lightweight profile data collection

  * Starts and stops msPTI Monitor to collect data.
  * Obtains the profile data collected by msPTI Monitor online.
  * Exports the profile data collected by msPTI Monitor locally in Excel and Chrome Trace JSON format.

### PyDynamicMonitorProxy APIs

This class handles IPC with the dynolog daemon and sends registration requests and profiler configuration parameters to it. You do not need to call these APIs directly.

* `init_dyno` sends a registration request to the dynolog daemon.
  * Input: npu_id(int)
  * Return: None
* `poll_dyno` obtains the profiler control parameters from the dynolog daemon.
  * Input: None
  * Return: str, the control parameters.
* `enable_dyno_npu_monitor` enables msPTI monitoring.
  * Input: cfg_map(Dict[str,str]), parameter configuration
  * Return: None
* `finalize_dyno` releases the resources and threads held by msmonitor.
  * Input: None
  * Return: None
* `update_profiler_status` reports the profiler status.
  * Input: status(Dict[str,str])
  * Return: None

### Monitor Feature APIs

For usage examples of the Monitor APIs, see [monitor_feature.md](./monitor_feature.md).

### ActivityKind Enumeration

This enumeration defines the data types that msPTI Monitor can collect and is used to configure the monitor module. Each enumerated value corresponds to one data collection type of msPTI Monitor.

  * ActivityKind.Marker: Collects mstx marker data and returns the Marker data structure.
  * ActivityKind.Kernel: Collects duration data of compute operators and returns the Kernel data structure.
  * ActivityKind.Communication: Collects duration data of communication operators and returns the Communication data structure.
  * ActivityKind.API: Collects duration data of operator API calls and returns the API data structure.
  * ActivityKind.AclAPI: Collects duration data of ACL API calls and returns the API data structure.
  * ActivityKind.NodeAPI: Collects duration data of Node API calls and returns the API data structure.
  * ActivityKind.RuntimeAPI: Collects duration data of Runtime component API calls and returns the API data structure.

### Monitor APIs

* `start` starts monitor data collection.
  * Input:
    * kinds(List[ActivityKind]) List of data collection types (existing capability)
    * dcmi_layers(List[DcmiLayer], optional) DCMI configuration by hardware layer. Selecting a layer collects every metric available for that layer; leave it empty to collect no DCMI data.
    * dcmi_interval_ms(int, optional, default: 10) DCMI sampling interval, in milliseconds
    * save_path(str, optional, default: None) Directory for saving the results. When it is None (the default), no session directory is created and no log is written to disk, and the data is obtained online only through `get_result()`; when it is specified, `start` creates the session directory `msmonitor_<pid>_<timestamp>/` under it, and glog writes directly into its `log/` from the very beginning.
  * Return: None
  * Note: No device needs to be specified for DCMI collection. The device already set by the current process is used by default.
* `stop` stops monitor data collection.
  * Input: None
  * Return: None
* `get_result` obtains the profile data collected by monitor.
  * Input: None
  * Return: Dict whose keys are the enabled `ActivityKind` values plus a DCMI sample list, which is always keyed `DCMI`
* `save` saves the profile data and runtime logs collected by monitor.
  * Input: None (the output directory was already fixed by start(save_path))
  * Return: None
  * Note: Generates `monitor_result.xlsx` (Excel, multiple sheets, including the DCMI sheet) and `monitor_result.json` (Chrome Trace JSON that can be opened in chrome://tracing or Perfetto, with kernel/comm duration bars and DCMI lines presented per hardware layer in separate processes and the DFX status embedded in the metadata) in the session directory `<save_path>/msmonitor_<pid>_<timestamp>/` fixed by `start`; the glog log is written directly into its `log/` subdirectory from start, with no move or copy; each file contains only the data that was actually collected.

### DcmiLayer Enumeration

Defines the hardware layers for DCMI collection and is the configuration unit of `start(dcmi_layers=...)`, corresponding one-to-one with the layered presentation in Chrome Trace. Selecting a layer collects every metric available for that layer:

* `DcmiLayer.DEVICE`: Device-level metrics: Power (W) and Temp (℃).
* `DcmiLayer.AICORE`: AICore current and rated frequency (MHz), and AI Core/AI Cube/Vector Core/NPU utilization (%).
* `DcmiLayer.AICPU`: AICPU maximum operating frequency (MHz), current frequency (MHz), and utilization (%).
* `DcmiLayer.HBM`: On-chip memory frequency (MHz), total capacity (MB), used memory (MB), bandwidth utilization (%), and temperature (℃).

> Compatibility note: The DCMI interface surface is identical across chip generations (A2/A3/A5). Types unsupported by the driver (for example, an overall NPU utilization that is unavailable on some chips) have the corresponding metrics disabled automatically, and the reason is printed to the startup log and written to the trace metadata.

### ActivityData Data Structure

Defines the profile data structure collected by monitor.

#### Marker Structure Fields

* `name` (str): Content of the mstx marker message
* `sourceKind` (str): Message source type, either `Host` or `Device`
* `domain` (str): Name of the domain to which the message belongs
* `id` (int): Message ID
* `startNs` (int): mstx marker start time, in nanoseconds
* `endNs` (int): mstx marker end time, in nanoseconds
* `pid` (int): Process ID when `sourceKind` is `Host`, or `0` when `sourceKind` is `Device`
* `tid` (int): Thread ID when `sourceKind` is `Host`, or `0` when `sourceKind` is `Device`
* `deviceId` (int): ID of the device to which the marker belongs when `sourceKind` is `Device`, or `0` when `sourceKind` is `Host`
* `streamId` (int): ID of the stream to which the marker belongs when `sourceKind` is `Device`, or `0` when `sourceKind` is `Host`

#### Kernel Structure Fields

* `name` (str): Name of the compute operator
* `startNs` (int): Operator execution start time, in nanoseconds
* `endNs` (int): Operator execution end time, in nanoseconds
* `deviceId` (int): ID of the device where the operator is executed
* `streamId` (int): ID of the stream where the operator is executed
* `correlationId` (int): Operator correlation ID, used to associate the operator with API data
* `type` (str): Operator type, for example, `KERNEL_AICORE`, `KERNEL_AIVEC`, or `KERNEL_AICPU`

#### Communication Structure Fields

* `name` (str): Name of the communication operator
* `startNs` (int): Operator execution start time, in nanoseconds
* `endNs` (int): Operator execution end time, in nanoseconds
* `deviceId` (int): ID of the device where the operator is executed
* `streamId` (int): ID of the stream where the operator is executed
* `count` (int): Volume of data transferred by the operator
* `dataType` (str): Data type transferred by the operator, for example, `FP32` or `INT8`
* `commName` (str): Name of the communication domain to which the operator belongs
* `algType` (str): Communication algorithm used by the operator, for example, `RING` or `MESH`
* `correlationId` (int): Operator correlation ID, used to associate the operator with API data

#### API Structure Fields

* `name` (str): API name
* `startNs` (int): API call start time, in nanoseconds
* `endNs` (int): API call end time, in nanoseconds
* `pid` (int): ID of the process that calls the API
* `tid` (int): ID of the thread that calls the API
* `correlationId` (int): API call correlation ID, used to associate the API with Kernel/Communication data

#### DcmiSample Structure Fields

A DCMI sample (an element of the `DCMI` list returned by `get_result()`):

* `kind` (int): Metric type (internal enumeration value, corresponding to the metric name in the exported metadata)
* `timestampNs` (int): Sampling timestamp, in nanoseconds
* `deviceId` (int): Device ID (flattened index)
* `value` (float): Sampled value (the unit depends on the metric type, for example, W for power, MHz for frequency, % for utilization, and MB for memory)

## Installation

For details about how to install the mindstudio_monitor module, see the *[msMonitor Installation Guide](../install_guide/msmonitor_install_guide.md)*.
