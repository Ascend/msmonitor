# msMonitor Development Guide

<br>

This document describes how to set up the msMonitor development environment, compile and build the project, and run UT unit tests.

## 1. Preparing the Build Environment

### 1.1 Method 1: devcontainer One-Click Development Environment (Recommended)

msmonitor has a built-in [devcontainer](https://containers.dev/) development environment configuration. After opening the repository in VS Code, developers can enter a standardized container with one click without manually installing any dependencies. The container automatically performs the following preparations:

- Python 3 environment and build toolchain (GCC 11.2.0, CMake 3.16+)
- System dependency installation (python3-devel, pip including pre-commit/bandit)
- pre-commit automatically enabled (including gitleaks secret scanning)
- clangd C++ language service ready
- Git identity synchronization

**Prerequisites:**

| Environment | Requirement |
|------|------|
| PC | VS Code with the Dev Containers extension and the Remote-SSH extension installed |
| Linux server | The Docker service is running |

**Procedure:**

1. Clone the msmonitor repository to the Linux server
2. Connect VS Code to the server through Remote-SSH and open the repository directory
3. VS Code automatically detects the `.devcontainer` configuration. Click **"Reopen in Container"** in the lower-left corner
4. After the container starts, `post-create.sh` is automatically executed to complete initialization (about 1 minute)
5. Press `Ctrl+Shift+P` → `Tasks: Run Task` to select a build task

**VS Code built-in tasks:**

| Task | Shortcut | Description |
|------|--------|------|
| `Build: Release Mode` | `Ctrl+Shift+B` | One-click build of whl + rpm/deb packages |
| `Build: Debug Mode` | — | Debug compilation of the C++ plugin (with debug symbols) |
| `Test: Run Unit Tests` | — | Run all unit tests |
| `Clean: All Workspace` | — | Clean all build artifacts (plugin/build, dist/, artifacts/, third_party/dynolog/, etc.) |

**Code smart navigation:**

- C++: After building, clangd provides cross-file navigation (F12), find references (Shift+F12), and code completion
- Python: Pylance provides semantic navigation and type inference

**Graphical debugging:**

- Open a Python source file and press `F5` to select `Python: Debug Active File` to enter debugpy debugging
- Breakpoints, variable inspection, call stacks, and other functions can all be used normally

### 1.2 Method 2: Manual Environment Configuration

Complete the setup of the build and test environment according to the chapter *[msMonitor Tool Installation Guide — Preparing the Build Environment](../install_guide/msmonitor_install_guide.md#231-environment-preparation)*.

> **Note:** The method for constructing the environment image and the accompanying software versions are maintained by the unified MindStudio image creation guide and are not redefined in this repository.

**Additional tools required for the manual environment:**

The manual environment does not use the automatic initialization of `post-create.sh` in devcontainer. You need to ensure by yourself that the following tools are available in `PATH`:

| Tool | Installation Method | Description |
|------|---------|------|
| gitleaks | Download the binary from [GitHub Releases](https://github.com/gitleaks/gitleaks/releases) and put it in `PATH`, or run `go install github.com/gitleaks/gitleaks/v8@latest` | Required for pre-commit secret scanning. If missing, `git commit` reports `gitleaks: command not found` |
| pre-commit | `pip install pre-commit && pre-commit install` | Automatic check before commit. You need to run `pre-commit install` in the repository root directory to install the hook |
| clangd | `dnf install clang-tools-extra` or `apt-get install clangd` | C++ code navigation and completion (optional) |

## 2. Configuring the TLS Certificate Environment

If the development and test scenario requires verifying TLS communication between the dyno CLI and the dynolog daemon, you need to prepare additional client and server certificate directories. For the directory specifications, see Section 5 of the *[Installation Guide](../install_guide/msmonitor_install_guide.md)*.

## 3. Development Steps

### 3.1 Code Download

After entering the interactive container shell opened by `ctr_in.py`, run the following command to clone this repository:

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

1. Check the GCC and Rust versions.
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
| Quick experience process | `docs/en/quick_start/npumonitor_quick_start.md` |
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
