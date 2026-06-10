# dyno使用说明

## ℹ️ 简介

dyno负责发送dyno CLI的RPC请求，触发nputrace和npu-monitor功能。

## 📘 使用指南

dyno命令的使用方式如下：

```bash
dyno [OPTIONS] --certs-dir <CERTS_DIR> <SUBCOMMAND> <SUBCOMMAND_ARGS>
```

## ⚙️ 功能介绍

**参数说明**

| 命令        | 参数类型 | 说明                                                         | 是否必选 |
| ----------- | -------- | ------------------------------------------------------------ | :------: |
| --hostname  | String   | dynolog daemon进程的主机名，默认值localhost。                |    N     |
| --port      | i32      | dynolog daemon进程监听的端口号，默认值1778。                 |    N     |
| --certs-dir | String   | 用于指定dyno与dynolog RPC通信时TLS证书的路径，当值为`NO_CERTS`时不使用证书校验，默认值`NO_CERTS`。 |    N     |
| --help      | action   | 用于获取dyno命令的使用帮助，查看所有可用选项和功能说明。     |    N     |
| --version   | action   | 用于查询dyno CLI的版本信息。                                 |    N     |

**常用子命令**

| 命令        | 说明                                                         |
| ----------- | ------------------------------------------------------------ |
| status      | 查询nputrace或者npu-monitor命令的执行状态。                  |
| nputrace    | 发送nputrace相关消息到dynolog daemon，详情请参见[nputrace](./nputrace_instruct.md)。 |
| npu-monitor | 发送npu-monitor相关消息到dynolog daemon，详情请参见[npu-monitor](./npumonitor_instruct.md)。 |
| help        | 获取dyno命令的使用帮助，查看所有可用选项和功能说明。         |
| version     | 查询dynolog daemon的版本信息。                               |
