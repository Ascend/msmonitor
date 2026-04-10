# 开发指南

## 1. MindStudio Monitor开发软件

| 软件名 | 用途 |
| --- | --- |
| CLion（推荐）/ VS Code | 编写和调试 `dynolog_npu` C++ 代码 |
| PyCharm（推荐）/ VS Code | 编写和调试 `plugin` 下的 Python/CMake 代码 |
| Git | 拉取、管理和提交代码 |
| CMake / Ninja | 本地构建与调试 |
| Python 虚拟环境工具（venv） | 隔离 Python 依赖 |

## 2. 开发环境配置

| 软件名 | 版本要求 | 用途 |
| --- | --- | --- |
| gcc | 8.5.0 及以上 | 编译 `dynolog_npu` |
| Rust | 1.81 及以上 | 编译 dynolog 相关依赖 |
| protobuf | 3.12 及以上 | dynolog / tensorboard 相关依赖 |
| Python | 与目标 whl 安装环境匹配 | 编译和安装 `mindstudio_monitor` |
| pybind11 | 最新稳定版 | 构建 `plugin` Python 扩展 |
| CMake | 最新稳定版 | CMake 构建 |
| Ninja | 最新稳定版 | 本地构建工具 |

### 2.1 依赖准备

根据当前安装指南，编译环境建议至少准备以下依赖：

```bash
# Debian / Ubuntu
sudo apt-get install -y cmake ninja-build
sudo apt install -y protobuf-compiler libprotobuf-dev

# Python
pip install pybind11 wheel protobuf
```

Rust 建议使用官方 `rustup` 安装：

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

### 2.2 TLS 证书环境

若开发和测试场景需要验证 dyno CLI 与 dynolog daemon 的 TLS 通信，需要额外准备客户端和服务端证书目录。目录规范可参见 [《安装指南》](../getting_started/install_guide.md)。

## 3. 开发步骤

### 3.1 代码下载

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

### 3.2 项目结构说明

当前仓库主要由以下模块组成：

| 目录 | 说明 |
| --- | --- |
| `dynolog_npu` | dynolog_npu 模块代码目录 |
| `dynolog_npu/cli` | dyno 客户端源代码 |
| `dynolog_npu/dynolog` | dynolog 服务端源代码 |
| `plugin` | Python 插件与 IPCMonitor 相关代码 |
| `plugin/ipc_monitor` | IPCMonitor 核心代码 |
| `plugin/IPCMonitor` | Python 包目录 |
| `scripts` | 构建、补丁、UT、ST 脚本 |
| `test/ut` | 单元测试 |
| `test/st` | 系统测试 |
| `third_party/dynolog` | dynolog 子模块和三方依赖 |
| `docs/zh` | 中文文档 |

### 3.3 `dynolog_npu` 开发

`dynolog_npu` 主要负责 dyno CLI 和 dynolog daemon 两部分能力。

开发时重点关注：

| 路径 | 说明 |
| --- | --- |
| `dynolog_npu/cli/src` | dyno CLI 代码 |
| `dynolog_npu/dynolog/src` | dynolog daemon 代码 |
| `dynolog_npu/cmake` | 构建配置 |
| `dynolog_npu/scripts/rpm` | RPM 打包相关文件 |

适用场景：

1. 新增或修改 dyno 子命令。
2. 扩展 dynolog daemon 的请求处理逻辑。
3. 调整守护进程上报、采集、展示相关逻辑。
4. 调整 deb / rpm 打包逻辑。

### 3.4 `plugin` 开发

`plugin` 模块提供 `mindstudio_monitor` whl 包、IPCMonitor 和 MSPTI Monitor 公共能力。

开发时重点关注：

| 路径 | 说明 |
| --- | --- |
| `plugin/setup.py` | whl 构建入口 |
| `plugin/CMakeLists.txt` | 插件模块 CMake 构建 |
| `plugin/bindings.cpp` | Python 扩展绑定入口 |
| `plugin/ipc_monitor` | IPCMonitor 核心代码 |
| `plugin/IPCMonitor` | Python 包内容 |
| `plugin/stub` | 构建 stub 的相关脚本与代码 |

适用场景：

1. 扩展 IPCMonitor 能力。
2. 增加 Python 接口或公共模块。
3. 调整 pybind11 扩展绑定。
4. 调整 whl 打包内容。

### 3.5 常见开发场景

#### 3.5.1 开发 `npu-monitor`

若本次改动涉及 `npu-monitor`：

1. 重点关注 dyno CLI 参数处理。
2. 重点关注 dynolog daemon 的监控请求下发和后台采集逻辑。
3. 若涉及 MSPTI 侧数据处理，需要联动 `plugin` 模块。
4. 同步更新 `docs/zh/user_guide/npumonitor_instruct.md`。

#### 3.5.2 开发 `nputrace`

若本次改动涉及 `nputrace`：

1. 重点关注 dyno 请求参数和 daemon 触发逻辑。
2. 核对与框架 Profiler、CANN 和 Device 侧数据采集的联动逻辑。
3. 若涉及日志、输出路径、离线解析或展示，需同步验证端到端流程。
4. 同步更新 `docs/zh/user_guide/nputrace_instruct.md`。

#### 3.5.3 开发 Monitor API

若本次改动涉及 Python API 或公共能力：

1. 优先关注 `plugin/IPCMonitor`、`plugin/ipc_monitor`。
2. 若涉及扩展模块暴露，需同步检查 `bindings.cpp` 和 `setup.py`。
3. 同步更新 `docs/zh/advanced_features/monitor_feature.md` 和 `docs/zh/advanced_features/mindstudio_monitor_api_reference.md`。

## 4. 构建与安装

### 4.1 构建 dynolog

仓库提供统一构建脚本 `scripts/build.sh`。该脚本会：

1. 检查 gcc 和 Rust 版本。
2. 初始化并切换 `third_party/dynolog` 子模块到指定提交。
3. 生成并应用 Ascend 相关补丁。
4. 构建 dyno 和 dynolog，或打包成 deb / rpm。

常用命令如下：

```bash
# 构建 dyno 和 dynolog 二进制
bash scripts/build.sh

# 构建 deb 包
bash scripts/build.sh -t deb

# 构建 rpm 包
bash scripts/build.sh -t rpm
```

### 4.2 构建并安装 `mindstudio_monitor`

#### 方式一：一键安装

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

#### 方式二：手动构建 whl

```bash
cd plugin
bash ./stub/build_stub.sh
python3 setup.py bdist_wheel
```

构建完成后在 `plugin/dist` 下生成 whl 包，随后执行：

```bash
cd plugin/dist
pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
```

### 4.3 本地运行验证

构建安装完成后，建议至少验证以下能力：

```bash
# 启动 dynolog daemon
dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs

# 启动 npu-monitor
dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel

# 触发 nputrace
dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
```

## 5. 测试与验证

### 5.1 单元测试

仓库提供 `scripts/run_ut.sh` 作为单元测试入口。

```bash
# 运行全部 UT 构建与测试
bash scripts/run_ut.sh

# 仅运行 plugin 相关测试
bash scripts/run_ut.sh plugin
```

当前脚本会：

1. 在 `test/build_llt` 下执行 CMake 构建。
2. 默认构建 `ut` 目标。
3. 执行 `test/ut/plugin/ipc_monitor` 下的可执行测试文件。

### 5.2 系统测试

仓库提供 `scripts/run_st.sh` 作为系统测试入口。

```bash
bash scripts/run_st.sh
```

当前系统测试主要执行：

- `test/st/test_dynolog_build.py`

以及 `test/st` 目录下其他符合规则的 Python 测试文件。

### 5.3 常见测试资源

测试目录如下：

| 目录 | 说明 |
| --- | --- |
| `test/ut/plugin/ipc_monitor` | IPCMonitor 单元测试 |
| `test/st` | 系统测试 |
| `test/st/gen_tls_certs.sh` | 测试证书生成脚本 |

## 6. 文档联动更新

功能开发完成后，若改动影响用户行为、部署方式或接口定义，需要同步更新文档。

| 改动类型 | 需同步更新的文档 |
| --- | --- |
| 安装、编译、升级、卸载 | `docs/zh/getting_started/install_guide.md` |
| 快速体验流程 | `docs/zh/getting_started/quick_start.md` |
| dynolog 服务端 | `docs/zh/user_guide/dynolog_instruct.md` |
| dyno 客户端 | `docs/zh/user_guide/dyno_instruct.md` |
| npu-monitor 功能 | `docs/zh/user_guide/npumonitor_instruct.md` |
| nputrace 功能 | `docs/zh/user_guide/nputrace_instruct.md` |
| Monitor API | `docs/zh/advanced_features/monitor_feature.md` |
| API 参考 | `docs/zh/advanced_features/mindstudio_monitor_api_reference.md` |
| 版本发布信息 | `docs/zh/release_notes.md` |

## 7. 提交流程建议

1. 功能开发完成后，先完成本地构建验证。
2. 若涉及 dynolog 补丁或打包逻辑，至少验证一次 `scripts/build.sh`。
3. 若涉及 plugin 改动，至少验证一次 whl 构建和安装。
4. 至少执行相关 UT，必要时补充 ST。
5. 若涉及用户可见行为变化，同步更新文档和示例命令。
