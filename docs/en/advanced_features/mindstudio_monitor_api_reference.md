# mindstudio_monitor Interfaces

## mindstudio_monitor Module

Provides inter-process communication (IPC) interfaces and the capability of independently controlling msPTI Monitor to collect and obtain profile data.

1. IPC control channel: The profiler backend obtains the profiler configuration from the dynolog daemon.
2. IPC data channel: msPTI Monitor sends profile data to the dynolog daemon.
3. Lightweight profile data collection

    * Starts or stops msPTI Monitor to collect data.
    * Obtains the profile data collected by msPTI Monitor online.
    * Export profile data collected by msPTI Monitor to the local host in Excel and Chrome Trace JSON formats

### PyDynamicMonitorProxy API

Communicates with the dynolog daemon through IPC, and sends registration requests and profiler configuration parameters to the dynolog daemon. You do not need to directly call this API.

* `init_dyno` sends registration requests to the dynolog daemon.
  * Input: npu_id(int)
  * Return: None
* `poll_dyno` obtains profiler control parameters from the dynolog daemon.
  * Input: None
  * Return: str, control parameter.
* `enable_dyno_npu_monitor` enables msPTI monitoring.
  * Input: cfg_map(Dict[str,str]) parameter configuration
  * Return: None
* `finalize_dyno` releases resources and threads in mindstudio_monitor.
  * Input: None
  * Return: None
* `update_profiler_status` reports the profiler status.
  * Input: status(Dict[str,str])
  * Return: None

### Monitor Feature APIs

For usage examples of the Monitor API, see [monitor_feature.md](./monitor_feature.md).

### ActivityKind Enumeration Class

This enumeration class defines the types of the data can be collected by msPTI Monitor and is used to configure the monitor module. Each enumerated value corresponds to a data type.

  * ActivityKind.Marker: collects mstx dotting data and returns the marker data structure.
  * ActivityKind.Kernel: collects the time consumption data of compute operators and returns the kernel data structure.
  * ActivityKind.Communication: collects the time consumption data of communication operators and returns the communication data structure.
  * ActivityKind.API: collects the time consumption data of operator API calls and returns the API data structure.
  * ActivityKind.AclAPI: collects the time consumption data of ACL API calls and returns the API data structure.
  * ActivityKind.NodeAPI: collects the time consumption data of Node API calls and returns the API data structure.
  * ActivityKind.RuntimeAPI: collects the time consumption data of Runtime API calls and returns the API data structure.

### Monitor API

* `start` starts monitor to collect data.
  * input:
    * kinds(List[ActivityKind]): List of data collection types (original capability).
    * dcmi_layers(List[DcmiLayer], optional): DCMI is configured by hardware layer. Selecting a layer collects all available metrics of that layer; if empty, DCMI is not collected.
    * dcmi_interval_ms(int, optional, default 10): DCMI sampling interval, unit: ms
    * save_path(str, optional, default None): Result save directory. When it is None (default), no session directory is created and no log is persisted; data is obtained online only through `get_result()`. When specified, start creates the `msmonitor_<pid>_<timestamp>/` session directory under it, and glog writes directly into its `log/` from the beginning.
  * Return: None
  * Note: The DCMI collection device does not need to be specified; by default, the device already set for the current process is used
* `stop` stops monitor to collect data.
  * Input: None
  * Return: None
* `get_result` obtains the profile data collected by monitor.
  * Input: None
  * Return: Dict, with keys being the enabled `ActivityKind` and the DCMI sample list (uniformly using `DCMI` as the key)
* `save` saves the profile data and running logs collected by monitor.
  * Input: None (the output directory has been determined in start(save_path))
  * Return: None
  * Note: Generates `monitor_result.xlsx` (Excel, multiple sheets, including the DCMI sheet) and `monitor_result.json` (Chrome Trace JSON, openable in chrome://tracing / Perfetto, with kernel/comm duration bars plus DCMI curves presented by hardware layer in separate processes, and DFX status embedded in metadata) in the session directory `<save_path>/msmonitor_<pid>_<timestamp>/` determined at start. glog logs are written directly into its `log/` subdirectory from start, with no moving or copying. Each file contains only the data actually collected

### DcmiLayer Enumeration Class

Defines the hardware layers collected by DCMI, as the configuration unit of `start(dcmi_layers=...)` (corresponding one-to-one with the layered presentation of Chrome Trace). Selecting a layer collects all available metrics of that layer:

* `DcmiLayer.DEVICE`: Device-level metrics Power (W) and Temp (°C)
* `DcmiLayer.AICORE`: AI Core current/rated frequency (MHz) and AI Core/AI Cube/Vector Core/NPU utilization (%)
* `DcmiLayer.AICPU`: AI CPU maximum running frequency (MHz), current frequency (MHz), and utilization (%)
* `DcmiLayer.HBM`: On-chip memory frequency (MHz), total capacity (MB), used memory (MB), bandwidth utilization (%), and temperature (℃)

> Compatibility note: The DCMI interface surface is consistent across different generations of chips (A2/A3/A5). Types not supported by the driver (for example, some chips do not have overall NPU utilization) automatically disable the corresponding metrics, and the reason is printed in the startup log and written into the trace metadata.

### ActivityData Data Structure

Defines the profile data structure collected by monitor.

#### Marker Structure Fields

* `name` (str): mstx dotting message content
* `sourceKind` (str): message source type, which can be `Host` or `Device`
* `domain` (str): name of the domain to which the message belongs
* `id` (int): message ID
* `startNs` (int): mstx dotting start time, in nanoseconds
* `endNs` (int): mstx dotting end time, in nanoseconds
* `pid` (int): process ID when `sourceKind` is `Host`, or `0` when `sourceKind` is `Device`
* `tid` (int): thread ID when `sourceKind` is `Host`, or `0` when `sourceKind` is `Device`
* `deviceId` (int): ID of the device to which the marker belongs when `sourceKind` is `Device`, or `0` when `sourceKind` is `Host`
* `streamId` (int): ID of the stream to which the marker belongs when `sourceKind` is `Device`, or `0` when `sourceKind` is `Host`

#### Kernel Structure Fields

* `name` (str): name of a compute operator
* `startNs` (int): operator execution start time, in nanoseconds
* `endNs` (int): operator execution end time, in nanoseconds
* `deviceId` (int): ID of the device where the operator is executed
* `streamId` (int): ID of the stream where the operator is executed
* `correlationId` (int): operator execution correlation ID, which is used to associate with API data
* `type` (str): operator type, for example, `KERNEL_AICORE`, `KERNEL_AIVEC`, or `KERNEL_AICPU`

#### Communication Structure Fields

* `name` (str): name of a communication operator
* `startNs` (int): operator execution start time, in nanoseconds
* `endNs` (int): operator execution end time, in nanoseconds
* `deviceId` (int): ID of the device where the operator is executed
* `streamId` (int): ID of the stream where the operator is executed
* `count` (int): data volume transmitted by the operator
* `dataType` (str): data type transmitted by the operator, for example, `FP32` or `INT8`
* `commName` (str): name of the communicator to which the operator belongs
* `algType` (str): communication algorithm type of the operator, for example, `RING` or `MESH`
* `correlationId` (int): operator execution correlation ID, which is used to associate with API data

#### API Structure Fields

* `name` (str): API name
* `startNs` (int): API call start time, in nanoseconds
* `endNs` (int): API call end time, in nanoseconds
* `pid` (int): ID of the process that calls the API
* `tid` (int): ID of the thread that calls the API
* `correlationId` (int): API call correlation ID, which is used to associate with kernel/communication data

#### DcmiSample Structure Fields

DCMI sampling sample (an element of the `DCMI` list returned by `get_result()`):

* `kind` (int): metric type (internal enumeration value, corresponding to the metric name in the export metadata)
* `timestampNs` (int): sampling timestamp, unit: ns
* `deviceId` (int): device ID (flat index)
* `value` (float): sampling value (the unit depends on the metric type; for example, power is in W, frequency in MHz, utilization in %, and memory in MB)

## Installation

For details about how to install the mindstudio_monitor module, see [msMonitor Installation Guide](../install_guide/msmonitor_install_guide.md).
