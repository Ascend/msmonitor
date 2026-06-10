<!-- markdownlint-disable MD033 MD041 -->
<h1 align="center">MindStudio Monitor</h1>

<div align="center">
  <p><b>昇腾集群在线性能监测与动态采集工具</b></p>

 [![QuickStart](https://badgen.net/badge/快速入门/QuickStart/blue)](./docs/zh/quick_start/quick_start.md)
 [![Ask DeepWiki](https://badgen.net/badge/AI问答/DeepWiki/blue)](https://deepwiki.com/mindstudio-docs/master)
 [![Ask ZRead](https://badgen.net/badge/AI问答/ZRead/blue)](https://zread.ai/mindstudio-docs/master)
 [![ReadTheDocs](https://badgen.net/badge/精确搜索/ReadTheDocs/blue)](https://mindstudio-docs-master.readthedocs.io)
 [![Community](https://badgen.net/badge/昇腾社区/Community/blue)](https://www.hiascend.com/cn/developer/software/mindstudio)
 [![Issues](https://badgen.net/badge/报告问题/Issues/blue)](https://gitcode.com/Ascend/msmonitor/issues)

</div>

## ✨ 最新消息

- [2025.12.30] MindStudio Monitor 项目全面开源

## ℹ️ 简介

MindStudio Monitor（`msMonitor`）是面向昇腾集群场景的在线性能监测与动态采集工具，基于 [dynolog][dynolog]（Meta CPU-GPU监控系统） 和 [msPTI][mspti]（MindStudio Profiler Tools Interface，MindStudio 性能分析工具接口）构建，支持`npu-monitor`、`nputrace`和`Monitor API`等能力。

支持框架 Profiler：[Ascend PyTorch Profiler][ascend-pytorch-profiler] |
[MindSpore Profiler][mindspore-profiler]

![msMonitor](./docs/zh/figures/msMonitor.png)

核心组件如下：

| 组件 | 作用 | 文档 |
| --- | --- | --- |
| `Dynolog daemon` | 服务端守护进程，负责接收 dyno 请求并触发监测与采集。 | [dynolog](./docs/zh/user_guide/dynolog_instruct.md) |
| `Dyno CLI` | 客户端命令行入口，用于下发 `npu-monitor` 和 `nputrace` 命令。 | [dyno](./docs/zh/user_guide/dyno_instruct.md) |
| `msPTI Monitor` | 基于 msPTI 的采集模块，负责获取并上报性能数据。 | - |

## ⚙️ 功能介绍

msMonitor 提供以下核心能力：

| 功能名称 | 功能简介 | 文档 |
| --- | --- | --- |
| **npu-monitor** | 轻量常驻后台，持续监测关键算子耗时，适合在线观察性能波动。 | [npu-monitor](./docs/zh/user_guide/npumonitor_instruct.md) |
| **nputrace** | 动态触发框架、CANN 和 Device 侧性能数据采集与解析，无需中断任务运行。 | [nputrace](./docs/zh/user_guide/nputrace_instruct.md) |
| **Monitor API** | 提供 Python 接口，采集计算类算子、通信类算子、API、Runtime API、Mstx 等性能数据。 | [Monitor API](./docs/zh/advanced_features/monitor_feature.md) |

> [!note]
>
> 由于底层资源限制，`npu-monitor` 与 `nputrace` 不能同时开启。

## 🚀 快速入门

首次使用 msMonitor 时，推荐直接按下面这条主线完成从安装到采集的端到端体验。
请参见 《[msMonitor 工具快速入门](./docs/zh/quick_start/quick_start.md)》。

## 📦 安装指南

msMonitor 工具安装指南包含如下内容：

- 下载软件包安装：适合直接部署使用，推荐优先采用。
- 编译软件包安装：适合源码调试、二次开发与定制构建。
- 升级、卸载与日志。

具体请参见《[msMonitor 工具安装指南](./docs/zh/install_guide/msmonitor_install_guide.md)》。

## 📘 使用指南

msMonitor 工具提供以下核心能力：**npu-monitor**、**nputrace**、**Monitor API**，详细使用说明请参见：<br>
🔹 [npu-monitor instruct](./docs/zh/user_guide/npumonitor_instruct.md) <br>
🔹 [nputrace instruct](./docs/zh/user_guide/nputrace_instruct.md) <br>
🔹 [Monitor API](./docs/zh/advanced_features/monitor_feature.md) <br>

## 💡 典型案例

msMonitor 在大模型训练&推理场景下的使用案例，请参见《[msMonitor使用案例](./docs/zh/best_practices/msmonitor_basic_cases.md)》。

## ❓ FAQ

常见问题及解决方案，请参见《[msMonitor FAQ](./docs/zh/support/faq.md)》。

## 🌌 智能检索

为提升文档查阅效率，我们提供多种高效检索方式：

🔹 [AI 问答（DeepWiki）](https://deepwiki.com/mindstudio-docs/master)：自然语言问答，快速把握项目架构与模块关系。<br>
🔹 [AI 问答（ZRead）](https://zread.ai/mindstudio-docs/master)：中文问答体验更优，精准定位功能用法与细节。<br>
🔹 [精确搜索（ReadTheDocs）](https://mindstudio-docs-master.readthedocs.io)：关键词全文检索，直达接口、参数与报错等信息。<br>

## 🛠️ 贡献指南

欢迎参与项目贡献，请参见 [《贡献指南》](./docs/zh/contributing/contributing_guide.md)。

## 📝 相关说明

🔹 [《版本说明》](./docs/zh/release_notes/release_notes.md) <br>
🔹 [《许可证声明》](./LICENSE) <br>
🔹 [《文档 License》](./docs/LICENSE) <br>
🔹 [《安全声明》](./docs/zh/legal/security_statement.md) <br>
🔹 [《免责声明》](./docs/zh/legal/disclaimer.md) <br>
🔹 [《漏洞机制说明》](./docs/zh/legal/mindstudio_vulnerability_handling_procedure.md) <br>
🔹 [《公网地址声明》](./docs/zh/legal/public_ip_address.md) <br>

## 🤝 建议与交流

欢迎大家通过 [Issues](https://gitcode.com/Ascend/msmonitor/issues)
反馈问题、需求和建议，我们会尽快响应。
若希望加入社区交流，也可以通过以下入口进一步了解 MindStudio 团队。

诚邀参与[满意度问卷调查](https://rdccucd.wjx.cn/vm/PKPfKqO.aspx)抽取惊喜好礼😎。

| 💬 即时互动（微信群） | 📢 官方资讯（公众号） | 深度支持（助手/论坛） |
| :---: | :---: | :--- |
| <img src="./docs/zh/figures/qr_code_wechat_work.png" width="120"><br><sub>*扫码直接加入技术交流群*</sub> | <img src="./docs/zh/figures/qr_code_wechat_official_account.png" width="120"><br><sub>*扫码关注获取最新动态*</sub> |欢迎扫码关注技术交流群跟官方公众号。这里是 MindStudio 用户与开发者最快捷的交流阵地：<br> **快速提问：** 与社区小伙伴即时探讨技术问题<br>**掌握动态：** 第一时间获取版本发布与功能更新通知<br> **经验共享：** 与广大开发者交流最佳实践与实战心得  <br>🛠️ **更多支持渠道**：<br>👉 昇腾助手：[![WeChat](https://img.shields.io/badge/WeChat-07C160?style=flat-square&logo=wechat&logoColor=white)](https://gitcode.com/Ascend/msit/blob/master/docs/zh/figures/readme/xiaozhushou.png)<br>👉 昇腾论坛：[![Website](https://img.shields.io/badge/Website-%231e37ff?style=flat-square&logo=RSS&logoColor=white)](https://www.hiascend.com/forum/) |

## 🙏 致谢

msMonitor 由华为公司的下列部门联合贡献：

- 昇腾计算 MindStudio 开发部
- 2012 欧拉实验室

感谢来自社区的每一个 Pull Request，欢迎贡献 msMonitor。

## 关于 MindStudio 团队

华为 MindStudio 全流程开发工具链团队致力于提供端到端的昇腾 AI
应用开发解决方案，帮助开发者高效完成训练开发、推理开发和性能调优。
更多信息可访问：

- [昇腾社区 MindStudio 专区](https://www.hiascend.com/developer/software/mindstudio)
- [昇腾论坛](https://www.hiascend.com/forum/)

[dynolog]: https://github.com/facebookincubator/dynolog
[mspti]: https://gitcode.com/Ascend/mspti/blob/master/docs/zh/getting_started/quick_start.md
[ascend-pytorch-profiler]: https://gitcode.com/Ascend/pytorch/blob/v2.7.1/docs/zh/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md
[mindspore-profiler]: https://gitcode.com/Ascend/docs/blob/master/MindStudio/master/mindspore_profiler_user_guide.md
