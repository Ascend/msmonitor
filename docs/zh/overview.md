# 总体介绍

## ℹ️ 简介

MindStudio Monitor（msMonitor）是面向昇腾集群场景的在线性能监测与动态采集工具， 基于 [dynolog](https://github.com/facebookincubator/dynolog)（Meta CPU-GPU监控系统）和 [msPTI](https://gitcode.com/Ascend/mspti/blob/master/docs/zh/quick_start/mspti_quick_start.md)（MindStudio Profiler Tools Interface，MindStudio 性能分析工具接口）构建，支持`npu-monitor`、`nputrace`和`Monitor API`等能力。

支持的框架Profiler工具：[Ascend PyTorch Profiler](https://gitcode.com/Ascend/pytorch/blob/v2.7.1/docs/zh/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md)和[MindSpore Profiler](https://gitcode.com/Ascend/docs/blob/master/MindStudio/master/mindspore_profiler_user_guide.md)

![msMonitor](./figures/msMonitor.png)

如上图所示msMonitor核心组件如下：

| 组件             | 作用                                                      | 文档                                        |
| ---------------- | --------------------------------------------------------- | ------------------------------------------- |
| `Dynolog daemon` | 服务端守护进程，负责接收dyno请求并触发监测与采集。        | [dynolog](./user_guide/dynolog_instruct.md) |
| `Dyno CLI`       | 客户端命令行入口，用于下发`npu-monitor`和`nputrace`命令。 | [dyno](./user_guide/dyno_instruct.md)       |
| `msPTI Monitor`  | 基于msPTI的采集模块，负责获取并上报性能数据。             | -                                           |

## 产品支持情况

> **说明：**
> 昇腾产品的具体型号，请参见《[昇腾产品形态说明](https://www.hiascend.com/document/detail/zh/AscendFAQ/ProduTech/productform/hardwaredesc_0001.html)》。

| 产品类型                                    | 是否支持 |
| ------------------------------------------- | :------: |
| Atlas 350 加速卡                            |    √     |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品   |    √     |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品   |    √     |
| Atlas 200I/500 A2 推理产品                  |    √     |
| Atlas 推理系列产品                           |    ×     |
| Atlas 训练系列产品                           |    ×     |

## ⚙️ 功能介绍

msMonitor提供以下核心功能：

| 功能名称        | 功能简介                                                     | 文档                                                  |
| --------------- | ------------------------------------------------------------ | ----------------------------------------------------- |
| **npu-monitor** | 轻量常驻后台，持续监测关键算子耗时，适合在线观察性能波动。   | [npu-monitor](./user_guide/npumonitor_instruct.md)    |
| **nputrace**    | 动态触发框架、CANN和Device侧性能数据采集与解析，无需中断任务运行。 | [nputrace](./user_guide/nputrace_instruct.md)         |
| **Monitor API** | 提供Python接口，采集计算类算子、通信类算子、API、Runtime API、Mstx等性能数据。 | [Monitor API](./advanced_features/monitor_feature.md) |

> [!NOTE]
>
> 由于底层资源限制，`npu-monitor`与`nputrace`不能同时开启。
