# Development Guide

<!-- md-trans-meta sourceCommit=76329dd28bc91c4378603d72fe34ab47b750c710 translatedAt=2026-08-14T01:54:04.785Z pushedAt=2026-08-14T01:55:14.971Z -->

## 1. MindStudio Monitor Development Software

| Software Name | Purpose |
| --- | --- |
| CLion (recommended) / VS Code | Write and debug the `dynolog_npu` C++ code. |
| PyCharm (recommended) / VS Code | Write and debug the Python/CMake code under `plugin.` |
| Git | Pull, manage, and commit code. |
| CMake / Ninja | Local build and debugging. |
| Python virtual environment tool (venv) | Isolate Python dependencies. |

## 2. Development Environment Configuration

| Software Name | Version Requirement | Purpose |
| --- | --- | --- |
| gcc | 8.5.0 and above | Compilation of `dynolog_npu` |
| Rust | 1.81 and above | Compilation of dynolog-related dependencies |
| protobuf | 3.12 and above | dynolog / TensorBoard-related dependencies |
| Python | Matching the target .whl installation environment | Compilation and installation of `mindstudio_monitor` |
| pybind11 | Latest stable | Building the `plugin` Python extension |
| CMake | Latest stable | CMake build |
| Ninja | Latest stable | Local build tool |

### 2.1 Dependency Preparation

According to the current installation guide, the compilation environment is recommended to prepare at least the following dependencies:

```bash
# Debian/Ubuntu
sudo apt-get install -y cmake ninja-build
sudo apt install -y protobuf-compiler libprotobuf-dev

# Python
pip install pybind11 wheel protobuf
```

Rust is recommended to be installed using the official `rustup`:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

### 2.2 TLS Certificate Environment

If the development and test scenarios require verifying TLS communication between dyno CLI and dynolog daemon, additional client and server certificate directories need to be prepared. For directory specifications, see [*Installation Guide*](../install_guide/msmonitor_install_guide.md).

## 3. Development Steps

### 3.1 Code Download

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

### 3.2 Project Structure Description

The current repository mainly consists of the following modules:

| Directory | Description |
| --- | --- |
| `dynolog_npu` | Source code directory of the dynolog_npu module |
| `dynolog_npu/cli` | Source code of the dyno client |
| `dynolog_npu/dynolog` | Source code of the dynolog server |
| `plugin` | Code related to Python plugins and IPCMonitor |
| `plugin/ipc_monitor` | Core code of IPCMonitor |
| `plugin/IPCMonitor` | Python package directory |
| `scripts` | Scripts for build, patch, UT, and ST |
| `test/ut` | Unit tests |
| `test/st` | System tests |
| `third_party/dynolog` | dynolog submodule and third-party dependencies |
| `docs/en` | Documentation |

### 3.3 `dynolog_npu` Development

`dynolog_npu` is primarily responsible for two capabilities: the dyno CLI and the dynolog daemon.

Key focus during development:

| Path | Description |
| --- | --- |
| `dynolog_npu/cli/src` | dyno CLI code |
| `dynolog_npu/dynolog/src` | dynolog daemon code |
| `dynolog_npu/cmake` | Build configuration |
| `dynolog_npu/scripts/rpm` | RPM packaging related files |

Applicable scenarios:

1. Add or modify dyno subcommands.

2. Extend the request processing logic of the dynolog daemon.

3. Adjust the daemon reporting, collection, and display logic.

4. Adjust the deb/rpm packaging logic.

### 3.4 `plugin` Development

The `plugin` module provides the `mindstudio_monitor` .whl package, IPCMonitor, and msPTI Monitor common capabilities.

Key focus during development:

| Path | Description |
| --- | --- |
| `plugin/setup.py` | .whl build entry |
| `plugin/CMakeLists.txt` | CMake build for the plugin module |
| `plugin/bindings.cpp` | Python extension binding entry |
| `plugin/ipc_monitor` | IPCMonitor core code |
| `plugin/IPCMonitor` | Python package content |
| `plugin/stub` | Scripts and code related to building the stub |

Applicable scenarios:

1. Extend IPCMonitor capabilities.

2. Add Python interfaces or common modules.

3. Adjust pybind11 extension bindings.

4. Adjust the whl packaging content.

### 3.5 Common Development Scenarios

#### 3.5.1 Developing `npu-monitor`

If this change involves `npu-monitor`:

1. Focus on dyno CLI parameter handling.

2. Focus on the monitoring request dispatch and background collection logic of the dynolog daemon.

3. For msPTI-side data processing, the `plugin` module needs to be linked.

4. Synchronously update `docs/en/user_guide/npumonitor_instruct.md`.

#### 3.5.2 Developing `nputrace`

If this change involves `nputrace`:

1. Focus on the dyno request parameters and daemon trigger logic.

2. Verify the linkage logic with the framework Profiler, CANN, and Device-side data collection.

3. If logs, output paths, offline parsing, or display are involved, verify the end-to-end process synchronously.

4. Synchronously update `docs/en/user_guide/nputrace_instruct.md`.

#### 3.5.3 Developing Monitor API

If this change involves Python APIs or common capabilities:

1. Prioritize `plugin/IPCMonitor` and `plugin/ipc_monitor`.

2. If the change involves exposing extension modules, check `bindings.cpp` and `setup.py` synchronously.

3. Synchronously update `docs/en/advanced_features/monitor_feature.md` and `docs/en/advanced_features/mindstudio_monitor_api_reference.md`.

## 4. Build and Installation

### 4.1 Building dynolog

The repository provides a unified build script `scripts/build.sh`. This script performs the following operations:

1. Check the gcc and Rust versions.

2. Initialize and switch the `third_party/dynolog` submodule to the specified commit.

3. Generate and apply Ascend-related patches.

4. Build dyno and dynolog, or package them into deb/rpm.

Common commands are as follows:

```bash
# Build the dyno and dynolog binaries
bash scripts/build.sh

# Build the deb package
bash scripts/build.sh -t deb

# Build the rpm package
bash scripts/build.sh -t rpm
```

### 4.2 Building and Installing `mindstudio_monitor`

#### Method 1: One-Click Installation

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

#### Method 2: Manually Build the .whl

```bash
cd plugin
bash ./stub/build_stub.sh
python3 setup.py bdist_wheel
```

After the build is complete, a .whl package is generated under `plugin/dist`. Then run the following commands:

```bash
cd plugin/dist
pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
```

### 4.3 Local Run Verification

After the build and installation are complete, it is recommended to verify at least the following capabilities:

```bash
# Start the dynolog daemon
dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs

# Start npu-monitor
dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel

# Trigger nputrace
dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
```

## 5. Testing and Verification

### 5.1 Unit Test

The repository provides `scripts/run_ut.sh` as the entry point for unit tests.

```bash
# Run all UT builds and tests
bash scripts/run_ut.sh

# Run only plugin-related tests
bash scripts/run_ut.sh plugin
```

The current script performs the following:

1. Run CMake build under `test/build_llt`.

2. Build the `ut` target by default.

3. Execute the executable test file under `test/ut/plugin/ipc_monitor`.

### 5.2 System Test

The repository provides `scripts/run_st.sh` as the system test entry point.

```bash
bash scripts/run_st.sh
```

The current system tests mainly execute:

- `test/st/test_dynolog_build.py`

and other Python test files in the `test/st` directory that comply with the rules.

### 5.3 Common Test Resources

The test directories are as follows:

| Directory | Description |
| --- | --- |
| `test/ut/plugin/ipc_monitor` | IPCMonitor unit test |
| `test/st` | System test |
| `test/st/gen_tls_certs.sh` | Test certificate generation script |

## 6. Document Synchronization Update

After feature development is completed, if the change affects user behavior, deployment methods, or interface definitions, the documentation must be synchronized and updated.

| Change Type | Documents to Be Synchronized and Updated |
| --- | --- |
| Installation, compilation, upgrade, uninstallation | `docs/en/install_guide/msmonitor_install_guide.md` |
| Quick experience process | `docs/en/quick_start/quick_start.md` |
| dynolog server | `docs/en/user_guide/dynolog_instruct.md` |
| dyno client | `docs/en/user_guide/dyno_instruct.md` |
| npu-monitor features | `docs/en/user_guide/npumonitor_instruct.md` |
| nputrace features | `docs/en/user_guide/nputrace_instruct.md` |
| Monitor API | `docs/en/advanced_features/monitor_feature.md` |
| API reference | `docs/en/advanced_features/mindstudio_monitor_api_reference.md` |
| Version release information | `docs/en/release_notes/release_notes.md` |

## 7. Submission Process Recommendations

1. After feature development is complete, perform local build verification first.

2. If the change involves dynolog patches or packaging logic, verify `scripts/build.sh` at least once.

3. If the change involves plugin modifications, verify the .whl build and installation at least once.

4. Execute the relevant Unit Tests at least, and supplement System Tests when necessary.

5. If user-visible behavior changes are involved, the documentation and example commands need to be updated.
