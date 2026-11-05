# dyno使用说明

## 简介

dyno负责发送dyno CLI的RPC请求，触发nputrace和npu-monitor功能。

## 使用指南

dyno命令的使用方式如下：

```bash
dyno [OPTIONS] <SUBCOMMAND> <SUBCOMMAND_ARGS>
```

`[OPTIONS]`为可选参数，具体介绍请参见[参数说明](#OPTIONS)；`<SUBCOMMAND>`为dyno的[常用子命令](#SUBCOMMAND)，`<SUBCOMMAND_ARGS>`为`<SUBCOMMAND>`的参数。

## 功能介绍

**参数说明**<a name="OPTIONS"></a>

| 命令        | 参数类型 | 说明                                                         | 是否必选 |
| ----------- | -------- | ------------------------------------------------------------ | :------: |
| --hostname  | String   | dynolog daemon进程的主机名，默认值localhost。                |    N     |
| --port      | i32      | dynolog daemon进程监听的端口号，默认值1778。                 |    N     |
| --certs-dir | String   | 用于指定dyno与dynolog RPC通信时TLS证书的路径，当值为`NO_CERTS`时不使用证书校验，默认值`NO_CERTS`。 |    N     |
| --help      | action   | 用于获取dyno命令的使用帮助，查看所有可用选项和功能说明。     |    N     |
| --version   | action   | 用于查询dyno CLI的版本信息。                                 |    N     |

**常用子命令**<a name="SUBCOMMAND"></a>

| 命令        | 说明                                                         |
| ----------- | ------------------------------------------------------------ |
| status      | 查询nputrace或者npu-monitor命令的执行状态，详情请参见[status](#status状态查询)。 |
| nputrace    | 发送nputrace相关消息到dynolog daemon，详情请参见[nputrace](./nputrace_instruct.md)。 |
| npu-monitor | 发送npu-monitor相关消息到dynolog daemon，详情请参见[npu-monitor](./npumonitor_instruct.md)。 |
| help        | 获取dyno命令的使用帮助，查看所有可用选项和功能说明。         |
| version     | 查询dynolog daemon的版本信息。                               |

## status状态查询

执行下述命令查看当前采集运行状态：

```bash
dyno status
```

命令执行后会输出一段 JSON 字符串示例：

```json
{"current_step":1,"npumonitor":"Idle","nputrace":"Ready","start_step":5,"stop_step":10,"trace_id":"123456789","queue_depth":2,"rejected":0}
```

### 字段说明

| 字段 | 说明 |
| --- | --- |
| `nputrace` | nputrace 采集状态，取值见[状态释义](#状态释义)。 |
| `npumonitor` | npu-monitor 采集状态，取值见[状态释义](#状态释义)。 |
| `current_step` | 当前执行到的步数，默认初始值为 `-1`。 |
| `start_step` / `stop_step` | 采集步数区间边界，仅当 `nputrace` 为 `Ready` 或 `Running` 时展示。 |
| `trace_id` | 当前正在采集、或下一个将在 `start_step` 启动的采集配置的 trace id。仅当 `nputrace` 为 `Ready` 或 `Running` 且模型进程支持上报时展示。 |
| `queue_depth` | 排队等待启动的采集配置数量（`Running` 时不含正在执行的配置）。仅当 `nputrace` 为 `Ready` 或 `Running` 且模型进程支持上报时展示。 |
| `rejected` | 模型进程累计拒绝的采集配置数量（如配置队列已满），模型进程支持上报时展示。 |

### 状态释义

- `Uninitialized`：进程未启动，或尚未执行 `dynolog init` 初始化。
- `Idle`：已初始化，暂无采集指令下发。
- `Ready`：采集指令已下发，当前步数尚未抵达采集起始 step。
- `Running`：正在进行数据采集。

### 补充说明

1. 字段状态集合
   - `nputrace`：支持 `Uninitialized` / `Idle` / `Ready` / `Running` 四种状态。
   - `npumonitor`：仅支持 `Uninitialized` / `Idle` / `Running` 三种状态。
2. `start_step`、`stop_step` 为采集步数区间边界，不同框架区间规则存在差异：
   - PyTorch：采集区间为左闭右开 `[start_step, stop_step)`，包含起始步、不包含终止步。
   - MindSpore：采集区间为闭区间 `[start_step, stop_step]`，起止步数均纳入采集范围。
3. 仅当 `nputrace` 状态为 `Ready` 或 `Running` 时，结果中才会展示 `start_step`、`stop_step` 字段。
4. `current_step` 默认初始值为 `-1`。
5. MindSpore 框架下，`nputrace` 不存在 `Ready` 状态。
6. `trace_id`、`queue_depth`、`rejected` 由模型进程上报，仅新版模型进程/插件提供：
   - `trace_id`、`queue_depth` 仅在 `nputrace` 为 `Ready` 或 `Running` 时展示；
   - `rejected` 在模型进程支持上报时展示；
   - 模型进程不支持上报时，结果中不出现对应字段，避免将“未上报”误读为 `0`。
7. 多个模型进程同时上报时，以最后一次上报为准。
