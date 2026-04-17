# mindstudio_monitor Interfaces

## mindstudio_monitor Module

Provides inter-process communication (IPC) interfaces and the capability of independently controlling MSPTI Monitor to collect and obtain profile data.

1. IPC control channel: The profiler backend obtains the profiler configuration from the dynolog daemon.
2. IPC data channel: MSPTI Monitor sends profile data to the dynolog daemon.
3. Lightweight profile data collection

  * Starts or stops MSPTI Monitor to collect data.
  * Obtains the profile data collected by MSPTI Monitor online.
  * Exports the profile data collected by MSPTI Monitor to the local PC in Excel format.

### PyDynamicMonitorProxy API

Communicates with the dynolog daemon through IPC, and sends registration requests and profiler configuration parameters to the dynolog daemon. You do not need to directly call this API.

* `init_dyno` sends registration requests the dynolog daemon.
  * Input: npu_id(int)
  * Return: None
* `poll_dyno` obtains profiler control parameters from the dynolog daemon.
  * Input: None
  * Return: str, control parameter.
* `enable_dyno_npu_monitor` enables MSPTI monitoring.
  * Input: cfg_map(Dict[str,str]) parameter configuration
  * Return: None
* `finalize_dyno` releases resources and threads in mindstudio_monitor.
  * Input: None
  * Return: None
* `update_profiler_status` reports the profiler status.
  * Input: status(Dict[str,str])
  * Return: None

### Monitor Feature APIs

### ActivityKind Enumeration Class

This enumeration class defines the types of the data can be collected by MSPTI Monitor and is used to configure the monitor module. Each enumerated value corresponds to a data type.

  * ActivityKind.Marker: collects mstx dotting data and returns the marker data structure.
  * ActivityKind.Kernel: collects the time consumption data of compute operators and returns the kernel data structure.
  * ActivityKind.Communication: collects the time consumption data of communication operators and returns the communication data structure.
  * ActivityKind.API: collects the time consumption data of operator API calls and returns the API data structure.
  * ActivityKind.AclAPI: collects the time consumption data of ACL API calls and returns the API data structure.
  * ActivityKind.NodeAPI: collects the time consumption data of Node API calls and returns the API data structure.
  * ActivityKind.RuntimeAPI: collects the time consumption data of Runtime API calls and returns the API data structure.

### Monitor API

* `start` starts monitor to collect data.
  * Input: kinds(List[ActivityKind]) data type list
  * Return: None
* `stop` stops monitor to collect data.
  * Input: None
  * Return: None
* `get_result` obtains the profile data collected by monitor.
  * Input: None
  * Return: Dict[ActivityKind, List[ActivityData]], profile data
* `save` saves the profile data collected by monitor.
  * Input: file_path(str) file path
  * Return: None

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

## Installation

For details about how to install the mindstudio_monitor module, see [msMonitor Installation Guide](./install_guide.md).
