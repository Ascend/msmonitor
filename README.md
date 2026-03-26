<!-- markdownlint-disable MD033 MD041 -->
<h1 align="center">MindStudio Monitor</h1>

<div align="center">
  <p><b>昇腾集群在线性能监控与动态采集工具</b></p>
  <p>
    <a href="./docs/zh/quick_start.md">🚀 快速入门</a> |
    <a href="./docs/zh/install_guide.md">🛠️ 安装指南</a> |
    <a href="./docs/zh/release_notes.md">📦 版本说明</a> |
    <a href="./docs/zh/overview.md">📖 工具文档</a> |
    <a href="https://gitcode.com/Ascend/msmonitor/issues">💬 问题反馈</a>
  </p>
</div>
<!-- markdownlint-enable MD033 MD041 -->

## 📢 最新消息

- [2025.12.30] msMonitor开源

## 📌 简介

MindStudio Monitor（`msMonitor`）是面向昇腾集群场景的在线性能监控与动态采集工具，
基于 [dynolog][dynolog] 和 [MSPTI][mspti] 构建，支持 `npu-monitor`、
`nputrace` 和 `Monitor API` 等能力。

支持框架 Profiler：[Ascend PyTorch Profiler][ascend-pytorch-profiler] |
[MindSpore Profiler][mindspore-profiler]

![msMonitor](./docs/zh/figures/msMonitor.png)

核心组件如下：

| 组件 | 作用 | 文档 |
| --- | --- | --- |
| `Dynolog daemon` | 服务端守护进程，负责接收 dyno 请求并触发监控与采集。 | [dynolog 使用说明](./docs/zh/dynolog_instruct.md) |
| `Dyno CLI` | 客户端命令行入口，用于下发 `npu-monitor` 和 `nputrace` 命令。 | [dyno 使用说明](./docs/zh/dyno_instruct.md) |
| `MSPTI Monitor` | 基于 MSPTI 的采集模块，负责获取并上报性能数据。 | - |

## 🔍 目录结构

关键目录如下，详细目录介绍请参见 [《项目目录》](./docs/zh/dir_structure.md)。

```text
├── docs                    # 项目文档目录
│   └── zh                  # 中文文档目录
├── dynolog_npu             # dynolog_npu 模块代码目录
├── plugin                  # 插件模块代码目录
├── scripts                 # 构建、测试等脚本目录
│   ├── build.sh            # dynolog_npu 构建脚本
│   ├── run_st.sh           # 系统测试脚本
│   └── run_ut.sh           # 单元测试脚本
├── test                    # 测试代码目录
│   ├── st                  # 系统测试用例
│   └── ut                  # 单元测试用例
├── third_party             # 第三方依赖库
├── CONTRIBUTING.md         # 贡献指南
└── README.md               # 项目说明文档
```

## 📖 功能介绍

msMonitor 提供以下核心能力：

| 功能名称 | 功能简介 | 说明文档 |
| --- | --- | --- |
| **npu-monitor** | 轻量常驻后台，持续监控关键算子耗时，适合在线观察性能波动。 | [点击查看](./docs/zh/npumonitor_instruct.md) |
| **nputrace** | 动态触发框架、CANN 和 Device 侧性能数据采集与解析，无需中断任务运行。 | [点击查看](./docs/zh/nputrace_instruct.md) |
| **Monitor API** | 提供 Python 接口，采集计算类算子、通信类算子、API、Runtime API、Mstx 等性能数据。 | [点击查看](./docs/zh/monitor_feature.md) |

> [!NOTE]
> 由于底层资源限制，`npu-monitor` 与 `nputrace` 不能同时开启。

## 🛠️ 安装指南

msMonitor 当前支持**软件包安装**和**编译安装**两种方式：

- 软件包安装：适合直接部署使用，推荐优先采用，详见
  [《msMonitor 工具安装指南》](./docs/zh/install_guide.md)。
- 编译安装：适合源码调试、二次开发与定制构建，详见
  [《msMonitor 工具安装指南》](./docs/zh/install_guide.md#编译安装)。
- 升级、卸载与日志：详见 [《msMonitor 工具安装指南》](./docs/zh/install_guide.md)。

## 🚀 快速入门

首次使用 msMonitor 时，推荐直接按下面这条主线完成从安装到采集的端到端体验。
更完整的安装说明请参见 [《安装指南》](./docs/zh/install_guide.md)。

1. 选择匹配版本并下载安装包。

   根据 [版本配套说明](#版本配套说明) 选择与当前 `CANN`、
   `torch_npu`、`MindSpore` 和 CPU 架构匹配的软件包，并下载到 Linux 环境。

2. 校验并安装 msMonitor 软件包。

   ```bash
   # 校验下载包
   sha256sum x86_8.3.0.zip

   # 解压安装包
   mkdir x86
   unzip x86_8.3.0.zip -d x86
   cd x86

   # 安装 whl 包，需选择与当前 Python 版本匹配的文件
   pip install \
     mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl

   # 安装 dynolog，按服务器系统选择其一
   dpkg -i --force-overwrite dynolog*.deb
   # rpm -ivh dynolog*.rpm --nodeps
   ```

3. 启动 `dynolog` daemon 进程。

   ```bash
   dynolog --enable-ipc-monitor --certs-dir /home/server_certs
   ```

4. 配置环境变量并启动训练或推理任务。

   ```bash
   export MSMONITOR_USE_DAEMON=1
   export LD_PRELOAD=<CANN安装路径>/ascend-toolkit/latest/lib64/libmspti.so

   bash run_ai_task.sh
   ```

5. 先使用 `npu-monitor` 观察关键算子耗时。

   ```bash
   dyno --certs-dir /home/client_certs npu-monitor \
     --npu-monitor-start --report-interval-s 30 \
     --mspti-activity-kind Kernel
   ```

6. 发现耗时劣化后，关闭 `npu-monitor` 并触发 `nputrace`
   采集详细数据。

   ```bash
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-stop
   dyno --certs-dir /home/client_certs nputrace \
     --start-step 10 --iterations 2 --activities CPU,NPU \
     --analyse --data-simplification false \
     --log-file /tmp/profile_data
   ```

7. 按需查看详细说明。

   - `npu-monitor` 使用说明：
     [`docs/zh/npumonitor_instruct.md`](./docs/zh/npumonitor_instruct.md)
   - `nputrace` 使用说明：
     [`docs/zh/nputrace_instruct.md`](./docs/zh/nputrace_instruct.md)
   - `MindSpore` 适配说明：
     [`docs/zh/mindspore_adapter_instruct.md`](./docs/zh/mindspore_adapter_instruct.md)

## 版本配套说明

msMonitor 由以下三个交付件组成：

<!-- markdownlint-disable MD013 -->
| 交付件 | 说明 |
| --- | --- |
| `dyno` | dyno 客户端二进制文件 |
| `dynolog` | dynolog 服务端二进制文件 |
| `mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl` | MSPTI Monitor、IPC 等公共能力工具包 |
<!-- markdownlint-enable MD013 -->

当前仓库维护的软件包版本如下，完整版本说明请参见
[《版本说明》](./docs/zh/release_notes.md)。

<!-- markdownlint-disable MD013 -->
| 版本 | 架构 | 发布日期 | CANN | torch_npu | MindSpore | 下载 | 校验码 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `8.3.0` | `aarch64` | `2025-12-29` | `8.3.RC1+` | `v7.3.0+` | `2.7.2+` | [aarch64_8.3.0.zip][pkg-830-aarch64] | `2c675ae346dfc1c70f5e9c7103d6f8c7e53be00dca28ed5f9cc577ac59e4bc44` |
| `8.3.0` | `x86` | `2025-12-29` | `8.3.RC1+` | `v7.3.0+` | `2.7.2+` | [x86_8.3.0.zip][pkg-830-x86] | `1a38cc141e67c50eb09ebdc757c1fd3ed54439f227459e71292b2d18bb78e7f0` |
| `8.1.0` | `aarch64` | `2025-07-11` | `8.1.RC1+` | `v7.1.0+` | `2.7.0-rc1+` | [aarch64_8.1.0.zip][pkg-810-aarch64] | `ce136120c0288291cc0a7803b1efc8c8416c6105e9d54c17ccf2e2510869fada` |
| `8.1.0` | `x86` | `2025-07-11` | `8.1.RC1+` | `v7.1.0+` | `2.7.0-rc1+` | [x86_8.1.0.zip][pkg-810-x86] | `097d11c7994793b6389b19259269ceb3b6b7ac5ed77da3949b3f09da2103b7f2` |
<!-- markdownlint-enable MD013 -->

## 📝 相关说明

- [《安全声明》](./docs/zh/security_statement.md)
- [《漏洞机制说明》](./docs/zh/mindstudio_vulnerability_handling_procedure.md)
- [《公网地址声明》](./docs/zh/public_ip_address.md)
- [《贡献指南》](./CONTRIBUTING.md)
- [《License》](./LICENSE)
- [《文档 License》](./docs/LICENSE)

## 联系我们

欢迎大家通过 [Issues](https://gitcode.com/Ascend/msmonitor/issues)
反馈问题、需求和建议，我们会尽快响应。
若希望加入社区交流，也可以通过以下入口进一步了解 MindStudio 团队。

<!-- markdownlint-disable MD033 MD013 -->
<div style="display: flex; align-items: center; gap: 10px;">
  <span>昇腾论坛：</span>
  <a href="https://www.hiascend.com/forum/" rel="nofollow">
    <img
      src="https://img.shields.io/badge/Website-%231e37ff?style=for-the-badge&logo=bytedance&logoColor=white"
      alt="昇腾论坛"
      style="max-width: 100%;"
    >
  </a>
  <span style="margin-left: 20px;">昇腾小助手：</span>
  <a href="https://raw.gitcode.com/kali20gakki1/Imageshack/raw/main/CDC0BEE2-8F11-477D-BD55-77A15417D7D1_4_5005_c.jpeg">
    <img
      src="https://img.shields.io/badge/WeChat-07C160?style=for-the-badge&logo=wechat&logoColor=white"
      alt="昇腾小助手二维码"
      style="max-width: 100%;"
    >
  </a>
</div>
<!-- markdownlint-enable MD033 MD013 -->

## 🤝 致谢

msMonitor 由华为公司的下列部门联合贡献：

- 昇腾计算 MindStudio 开发部

感谢来自社区的每一个 Pull Request，欢迎贡献 msMonitor。

## 关于 MindStudio 团队

华为 MindStudio 全流程开发工具链团队致力于提供端到端的昇腾 AI
应用开发解决方案，帮助开发者高效完成训练开发、推理开发和性能调优。
更多信息可访问：

- [昇腾社区 MindStudio 专区](https://www.hiascend.com/developer/software/mindstudio)
- [昇腾论坛](https://www.hiascend.com/forum/)

[dynolog]: https://github.com/facebookincubator/dynolog
[mspti]: https://www.hiascend.com/document/detail/zh/mindstudio/81RC1/T&ITools/Profiling/atlasprofiling_16_0021.html
[ascend-pytorch-profiler]: https://www.hiascend.com/document/detail/zh/mindstudio/81RC1/T&ITools/Profiling/atlasprofiling_16_0090.html#ZH-CN_TOPIC_0000002353635602__zh-cn_topic_0000002370275077_section17272160135118
[mindspore-profiler]: https://www.hiascend.com/document/detail/zh/mindstudio/81RC1/T&ITools/Profiling/atlasprofiling_16_0087.html
[pkg-830-aarch64]: https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.3.0/aarch64_8.3.0.zip
[pkg-830-x86]: https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.3.0/x86_8.3.0.zip
[pkg-810-aarch64]: https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.1.0/aarch64_8.1.0.zip
[pkg-810-x86]: https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.1.0/x86_8.1.0.zip
