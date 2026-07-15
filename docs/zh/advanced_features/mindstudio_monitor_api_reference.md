# mindstudio_monitor模块接口参考

## mindstudio_monitor模块介绍

提供IPC（Inter-Process Communication，进程间通信）接口以及独立控制 msPTI Monitor 采集和获取性能数据的能力。

1. IPC控制通道: profiler backend 向 dynolog daemon 获取 profiler 配置
2. IPC数据通道: msPTI Monitor 向 dynolog daemon 发送性能数据
3. 轻量化性能数据采集

  * 控制 msPTI Monitor 启停采集数据
  * 在线获取 msPTI Monitor 采集的性能数据
  * 将 msPTI Monitor 采集的性能数据以 Excel 格式导出到本地

### PyDynamicMonitorProxy接口说明

负责与 dynolog daemon 进行进程间通信（IPC），向其发送注册请求、Profiler 配置参数等，用户不需要直接调用该接口。

* `init_dyno` 向 dynolog daemon 发送注册请求
  * input: npu_id(int)
  * return: None
* `poll_dyno` 向 dynolog daemon 获取 Profiler 控制参数
  * input: None
  * return: str，返回控制参数
* `enable_dyno_npu_monitor` 开启msPTI监测
  * input: cfg_map(Dict[str,str]) 参数配置
  * return: None
* `finalize_dyno` 释放 msmonitor 中相关资源、线程
  * input: None
  * return: None
* `update_profiler_status` 上报 Profiler 状态
  * input: status(Dict[str,str])
  * return: None

### Monitor特性接口说明

Monitor API 使用示例请参见 [monitor_feature.md](./monitor_feature.md)。

### ActivityKind枚举类

该枚举类定义 msPTI Monitor 支持的数据采集类型，用于 monitor 模块的配置，每个枚举值对应 msPTI Monitor 的一种数据采集类型。

  * ActivityKind.Marker: 采集 mstx 打点数据，返回 Marker 数据结构
  * ActivityKind.Kernel: 采集计算类算子的耗时数据，返回 Kernel 数据结构
  * ActivityKind.Communication: 采集通信类算子的耗时数据，返回 Communication 数据结构
  * ActivityKind.API: 采集算子调用 API 的耗时数据，返回 API 数据结构
  * ActivityKind.AclAPI: 采集 ACL API 的调用耗时数据，返回 API 数据结构
  * ActivityKind.NodeAPI: 采集 Node API 的调用耗时数据，返回 API 数据结构
  * ActivityKind.RuntimeAPI: 采集 Runtime 组件 API 的调用耗时数据，返回 API 数据结构

### Monitor接口

* `start` 开启 monitor 数据采集
  * input: kinds(List[ActivityKind]) 数据采集类型列表
  * return: None
* `stop` 停止 monitor 数据采集
  * input: None
  * return: None
* `get_result` 获取 monitor 采集的性能数据
  * input: None
  * return: Dict[ActivityKind, List[ActivityData]]，返回性能数据
* `save` 保存 monitor 采集的性能数据
  * input: file_path(str) 保存文件路径
  * return: None

### ActivityData数据结构

定义 Monitor 采集的性能数据结构。

#### Marker结构体字段

* `name` (str): mstx打点消息内容
* `sourceKind` (str): 消息来源类型，"Host" 或 "Device"
* `domain` (str): 消息所属 domain 名称
* `id` (int): 消息ID
* `startNs` (int): mstx打点开始时间，单位：ns
* `endNs` (int): mstx打点结束时间，单位：ns
* `pid` (int): sourceKind 为 "Host" 时为进程ID，为 "Device" 时为 0
* `tid` (int): sourceKind 为 "Host" 时为线程ID，为 "Device" 时为 0
* `deviceId` (int): sourceKind 为 "Device" 时为marker所属设备ID，为 "Host" 时为 0
* `streamId` (int): sourceKind 为 "Device" 时为marker所属流ID，为 "Host" 时为 0

#### Kernel结构体字段

* `name` (str): 计算类算子名称
* `startNs` (int): 算子执行开始时间，单位：ns
* `endNs` (int): 算子执行结束时间，单位：ns
* `deviceId` (int): 算子执行所在的设备ID
* `streamId` (int): 算子执行所在的流ID
* `correlationId` (int): 算子执行关联ID，用于和API数据关联
* `type` (str): 算子类型，例如 "KERNEL_AICORE"、"KERNEL_AIVEC"、"KERNEL_AICPU" 等

#### Communication结构体字段

* `name` (str): 通信类算子名称
* `startNs` (int): 算子执行开始时间，单位：ns
* `endNs` (int): 算子执行结束时间，单位：ns
* `deviceId` (int): 算子执行所在的设备ID
* `streamId` (int): 算子执行所在的流ID
* `count` (int): 算子传输的数据量
* `dataType` (str): 算子传输的数据类型，例如 "FP32"、"INT8" 等
* `commName` (str): 算子所属通信域名称
* `algType` (str): 算子所属通信算法类型，例如 "RING"、"MESH" 等
* `correlationId` (int): 算子执行关联ID，用于和API数据关联

#### API结构体字段

* `name` (str): API 名称
* `startNs` (int): API 调用开始时间，单位：ns
* `endNs` (int): API 调用结束时间，单位：ns
* `pid` (int): 调用 API 的进程ID
* `tid` (int): 调用 API 的线程ID
* `correlationId` (int): API调用关联ID，用于和Kernel/Communication数据关联

## 安装方式

mindstudio_monitor模块安装请参见《[msMonitor工具安装指南](../install_guide/msmonitor_install_guide.md)》。
