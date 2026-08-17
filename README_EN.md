<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T10:55:36.511Z pushedAt=2026-08-12T10:56:45.356Z -->

<h1 align="center">MindStudio Monitor</h1>
<div align="center">
  <p><b>Online Performance Monitoring and Dynamic Collection Tool for Ascend Clusters</b></p>

 [![QuickStart](https://badgen.net/badge/QuickStart/QuickStart/blue)](./docs/en/quick_start/msmonitor_quick_start.md)
 [![Ask DeepWiki](https://badgen.net/badge/Ask%20AI/DeepWiki/blue)](https://deepwiki.com/mindstudio-docs/master)
 [![Ask ZRead](https://badgen.net/badge/Ask%20AI/ZRead/blue)](https://zread.ai/mindstudio-docs/master)
 [![ReadTheDocs](https://badgen.net/badge/Precise%20Search/ReadTheDocs/blue)](https://mindstudio-docs-master.readthedocs.io)
 [![Community](https://badgen.net/badge/Ascend%20Community/Community/blue)](https://www.hiascend.com/cn/developer/software/mindstudio)
 [![Issues](https://badgen.net/badge/Report%20Issues/Issues/blue)](https://gitcode.com/Ascend/msmonitor/issues)

</div>

English | [简体中文](./README.md)

## ✨ Latest News

- [2025.12.30] MindStudio Monitor is now fully open sourced.

## ℹ️ Introduction

MindStudio Monitor (`msMonitor`) is an online performance monitoring and dynamic profiling tool designed for Ascend cluster scenarios. Built on [dynolog][dynolog] (Meta CPU-GPU monitoring system) and [msPTI][mspti] (MindStudio Profiler Tools Interface), it supports capabilities such as `npu-monitor`, `nputrace`, and `Monitor API`.

Supported framework Profilers: [Ascend PyTorch Profiler][ascend-pytorch-profiler] | [MindSpore Profiler][mindspore-profiler]

![msMonitor](./docs/en/figures/msMonitor.png)

The core components are as follows:

| Component | Purpose | Documentation |
| --- | --- | --- |
| `Dynolog daemon` | Server-side daemon process, responsible for receiving dyno requests and triggering monitoring and collection. | [dynolog](./docs/en/user_guide/dynolog_instruct.md) |
| `Dyno CLI` | Client-side command-line entry for issuing `npu-monitor` and `nputrace` commands. | [dyno](./docs/en/user_guide/dyno_instruct.md) |
| `msPTI Monitor` | msPTI-based collection module, responsible for obtaining and reporting performance data. | - |

## ⚙️ Features

msMonitor provides the following core features:

| Feature Name | Feature Description | Documentation |
| --- | --- | --- |
| **npu-monitor** | A lightweight resident background service that continuously monitors the latency of key operators, suitable for online observation of performance fluctuations. | [npu-monitor](./docs/en/user_guide/npumonitor_instruct.md) |
| **nputrace** | Dynamically triggers performance data collection and parsing on the framework, CANN, and device sides without interrupting task execution. | [nputrace](./docs/en/user_guide/nputrace_instruct.md) |
| **Monitor API** | Provides Python interfaces for collecting performance data such as compute operators, communication operators, APIs, Runtime APIs, and Mstx. | [Monitor API](./docs/en/advanced_features/monitor_feature.md) |

> [!NOTE]
>
> Due to underlying resource limitations, `npu-monitor` and `nputrace` cannot be enabled simultaneously.

## 🚀 Quick Start

When using msMonitor for the first time, it is recommended to follow the main workflow below for an end-to-end experience from installation to collection. See [*msMonitor Tool Quick Start*](./docs/en/quick_start/msmonitor_quick_start.md).

## 📦 Installation Guide

The msMonitor tool installation guide includes the following:

- Download the software package for installation: suitable for direct deployment and use, recommended as the preferred method.

- Build the software package for installation: suitable for source code debugging, secondary development, and custom builds.

- Upgrade, uninstallation, and logs.

For details, see [*msMonitor Tool Installation Guide*](./docs/en/install_guide/msmonitor_install_guide.md).

## 📘 Usage Guide

The msMonitor tool provides the following core capabilities: **npu-monitor**, **nputrace**, and **Monitor API**. For detailed usage instructions, see:<br>
🔹 [npu-monitor instruct](./docs/en/user_guide/npumonitor_instruct.md) <br>
🔹 [nputrace instruct](./docs/en/user_guide/nputrace_instruct.md) <br>
🔹 [Monitor APIs](./docs/en/advanced_features/monitor_feature.md) <br>

## 💡 Typical Cases

For use cases of msMonitor in large model training and inference scenarios, see *[msMonitor Use Cases](./docs/en/best_practices/msmonitor_basic_cases.md)*.

## ❓ FAQs

For common issues and solutions, see *[msMonitor FAQs](./docs/en/support/faq.md)*.

## 🌌 Smart Search

To improve document lookup efficiency, we provide multiple efficient search methods:

🔹 [AI Q&A (DeepWiki)](https://deepwiki.com/mindstudio-docs/master): Natural language Q&A for quickly grasping the project architecture and module relationships.<br>
🔹 [AI Q&A (ZRead)](https://zread.ai/mindstudio-docs/master): Optimized Q&A experience for precisely locating feature usage and details.<br>
🔹 [Precision Search (ReadTheDocs)](https://mindstudio-docs-master.readthedocs.io): Keyword-based full-text search for directly accessing APIs, parameters, error messages, and more.<br>

## 🛠️ Contribution Guide

Contributions are welcome. See *[Contributing Guide](./docs/en/contributing/contributing_guide.md)*.

## ⚖️ Related Notes

🔹 *[Release Notes](https://gitcode.com/Ascend/msmonitor/releases)* <br>
🔹 *[License Notice](docs/en/legal/license_notice.md)* <br>
🔹 *[Security Statement](./docs/en/legal/security_statement.md)* <br>
🔹 *[Disclaimer](./docs/en/legal/disclaimer.md)* <br>

## 🤝 Suggestions and Communication

Everyone is welcome to contribute to the community. If you have any questions or suggestions, please submit [Issues](https://gitcode.com/Ascend/msmonitor/issues), and we will respond as soon as possible. Thank you for your support.

You are cordially invited to participate in the [Satisfaction Survey](https://rdccucd.wjx.cn/vm/PKPfKqO.aspx) for a chance to win a surprise gift 😎.

| 💬 Instant Interaction (WeChat Group) | 📢 Official News (Official Account) | In-Depth Support (Assistant/Forum) |
| :---: | :---: | :--- |
| <img src="./docs/en/figures/qr_code_wechat_work.png" width="120"><br><sub>*Scan the QR code to join the technical discussion group*</sub> | <img src="./docs/en/figures/qr_code_wechat_official_account.png" width="120"><br><sub>*Scan the QR code to follow and get the latest updates*</sub> |Scan the QR code to join the group and follow the official account, the fastest way for MindStudio users and developers to communicate:<br> **Quick Q&A:** Discuss technical issues with community members in real time<br>**Stay Updated:** Receive version release and feature update notifications as soon as they are available<br> **Experience Sharing:** Exchange best practices and hands-on insights with fellow developers  <br>🛠️ **More Support Channels**: 👉 Ascend Assistant: [![WeChat](https://img.shields.io/badge/WeChat-07C160?style=flat-square&logo=wechat&logoColor=white)](https://gitcode.com/Ascend/msit/blob/master/docs/zh/figures/readme/xiaozhushou.png)👉 Ascend Forum: [![Website](https://img.shields.io/badge/Website-%231e37ff?style=flat-square&logo=RSS&logoColor=white)](https://www.hiascend.com/forum/) |

## 🙏 Acknowledgments

This tool is jointly contributed by the following departments of Huawei:<br>
🔹 MindStudio Development Department, Ascend Computing<br>
🔹 2012 Euler Laboratory<br>
Thank you for every PR from the community. Contributions are welcome!

[dynolog]: https://github.com/facebookincubator/dynolog
[mspti]: https://gitcode.com/Ascend/mspti/blob/26.1.0/docs/en/quick_start/mspti_quick_start.md
[ascend-pytorch-profiler]: https://gitcode.com/Ascend/pytorch/blob/v2.7.1-26.1.0/docs/en/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md
[mindspore-profiler]: https://gitcode.com/Ascend/docs/blob/master/MindStudio/26.1.0/en/menu/mindspore_profiler_user_guide.md
