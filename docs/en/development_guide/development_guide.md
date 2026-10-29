# msMonitor Development Guide

<br>

This document describes how to set up a development environment, build msMonitor, and run unit tests (UTs).

## 1. Preparing the Build Environment

### 1.1 Option 1: One-Click devcontainer Environment (Recommended)

msmonitor ships with a built-in [devcontainer](https://containers.dev/) development environment configuration. After opening the repository in VS Code, developers can enter a standardized container with one click and do not need to install any dependencies manually. The container performs the following preparations automatically:

- Python 3 environment and build toolchain (GCC 11.2.0, CMake 3.16 or later)
- System dependency installation (python3-devel, and pip packages including pre-commit and bandit)
- pre-commit enabled automatically (including gitleaks secret scanning)
- clangd C++ language service ready
- Git identity synchronization

**Prerequisites:**

| Environment | Requirement |
|------|------|
| PC | VS Code with the Dev Containers and Remote-SSH extensions installed |
| Linux server | Docker service running |

**Steps:**

1. Clone the msmonitor repository to the Linux server.
2. Connect VS Code to the server over Remote-SSH and open the repository directory.
3. VS Code detects the `.devcontainer` configuration automatically. Click **Reopen in Container** in the lower-left corner.
4. After the container starts, `post-create.sh` runs automatically to complete initialization (about 1 minute).
5. Press `Ctrl+Shift+P` → `Tasks: Run Task` and select a build task.

**Built-in VS Code tasks:**

| Task | Shortcut | Description |
|------|--------|------|
| `Build: Release Mode` | `Ctrl+Shift+B` | Builds the whl and the rpm/deb packages in one step |
| `Build: Debug Mode` | — | Builds the C++ plugin in debug mode (with debug symbols) |
| `Test: Run Unit Tests` | — | Runs the full unit test suite |
| `Clean: All Workspace` | — | Cleans all build artifacts (plugin/build, dist/, artifacts/, third_party/dynolog/, and so on) |

**Code intelligence:**

- C++: after a build, clangd provides cross-file navigation (F12), find references (Shift+F12), and code completion.
- Python: Pylance provides semantic navigation and type inference.

**Graphical debugging:**

- Open a Python source file and press `F5`, then select `Python: Debug Active File` to start debugging with debugpy.
- Breakpoints, variable inspection, call stacks, and other features all work as expected.

### 1.2 Option 2: Manual Environment Setup

Set up the build and test environment by following the *[msMonitor Installation Guide — Environment Preparation](../install_guide/msmonitor_install_guide.md#231-environment-preparation)* section.

> **Note:** How to build the environment image and which software versions to pair with it are maintained by the unified MindStudio image creation guide and are not redefined in this repository.

**Tools additionally required for a manual environment:**

A manual environment does not go through the automatic initialization performed by `post-create.sh` in the devcontainer, so you must make sure the following tools are available on `PATH`:

| Tool | Installation | Description |
|------|---------|------|
| gitleaks | Download the binary from [GitHub Releases](https://github.com/gitleaks/gitleaks/releases) and put it on `PATH`, or run `go install github.com/gitleaks/gitleaks/v8@latest` | Required for pre-commit secret scanning. If it is missing, `git commit` fails with `gitleaks: command not found`. |
| pre-commit | `pip install pre-commit && pre-commit install` | Runs checks automatically before each commit. Run `pre-commit install` in the repository root to install the hook. |
| clangd | `dnf install clang-tools-extra` or `apt-get install clangd` | C++ code navigation and completion (optional) |

## 2. TLS Certificate Environment Configuration

If the development and test scenarios need to verify TLS communication between the dyno CLI and the dynolog daemon, client and server certificate directories must be prepared in addition. For the directory conventions, see section 5 of the [Installation Guide](../install_guide/msmonitor_install_guide.md).

## 3. Development Steps

### 3.1 Obtaining the Code

After entering the interactive container shell opened by `ctr_in.py`, run the following commands to clone this repository:

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

### 3.2 Project Structure

The repository currently consists of the following modules:

| Directory | Description |
| --- | --- |
| `dynolog_npu` | Source code directory of the dynolog_npu module |
| `dynolog_npu/cli` | Source code of the dyno client |
| `dynolog_npu/dynolog` | Source code of the dynolog server |
| `plugin` | Python plugin and IPCMonitor related code |
| `plugin/ipc_monitor` | Core code of IPCMonitor |
| `plugin/IPCMonitor` | Python package directory |
| `scripts` | Build, patch, UT, and ST scripts |
| `test/ut` | Unit tests |
| `test/st` | System tests |
| `third_party/dynolog` | dynolog submodule and third-party dependencies |
| `docs/en` | English documentation |

### 3.3 `dynolog_npu` Development

`dynolog_npu` is mainly responsible for two parts: the dyno CLI and the dynolog daemon.

Key areas during development:

| Path | Description |
| --- | --- |
| `dynolog_npu/cli/src` | dyno CLI code |
| `dynolog_npu/dynolog/src` | dynolog daemon code |
| `dynolog_npu/cmake` | Build configuration |
| `dynolog_npu/scripts/rpm` | RPM packaging related files |

Applicable scenarios:

1. Adding or modifying dyno subcommands.
2. Extending the request handling logic of the dynolog daemon.
3. Adjusting the daemon reporting, collection, and display logic.
4. Adjusting the deb/rpm packaging logic.

### 3.4 `plugin` Development

The `plugin` module provides the `mindstudio_monitor` whl package, IPCMonitor, and common msPTI Monitor capabilities.

Key areas during development:

| Path | Description |
| --- | --- |
| `plugin/setup.py` | whl build entry |
| `plugin/CMakeLists.txt` | CMake build for the plugin module |
| `plugin/bindings.cpp` | Python extension binding entry |
| `plugin/ipc_monitor` | Core code of IPCMonitor |
| `plugin/IPCMonitor` | Python package content |
| `plugin/stub` | Scripts and code related to building the stub |

Applicable scenarios:

1. Extending IPCMonitor capabilities.
2. Adding Python APIs or common modules.
3. Adjusting pybind11 extension bindings.
4. Adjusting the whl packaging content.

### 3.5 Common Development Scenarios

#### 3.5.1 Developing `npu-monitor`

If the change involves `npu-monitor`:

1. Focus on dyno CLI parameter handling.
2. Focus on the monitoring request dispatch and background collection logic of the dynolog daemon.
3. If msPTI-side data processing is involved, the `plugin` module must be updated as well.
4. Update `docs/en/user_guide/npumonitor_instruct.md` accordingly.

#### 3.5.2 Developing `nputrace`

If the change involves `nputrace`:

1. Focus on the dyno request parameters and the daemon trigger logic.
2. Check the linkage with the framework Profiler, CANN, and Device-side data collection.
3. If logs, output paths, offline parsing, or display are involved, verify the end-to-end flow as well.
4. Update `docs/en/user_guide/nputrace_instruct.md` accordingly.

#### 3.5.3 Developing Monitor APIs

If the change involves Python APIs or common capabilities:

1. Focus on `plugin/IPCMonitor` and `plugin/ipc_monitor` first.
2. If extension modules are exposed, check `bindings.cpp` and `setup.py` as well.
3. Update `docs/en/advanced_features/monitor_feature.md` and `docs/en/advanced_features/mindstudio_monitor_api_reference.md` accordingly.

## 4. Building and Installing

### 4.1 Building dynolog

The repository provides a unified build script `scripts/build.sh`, which performs the following operations:

1. Checks the GCC and Rust versions.
2. Initializes the `third_party/dynolog` submodule and switches it to the specified commit.
3. Generates and applies Ascend-related patches.
4. Builds dyno and dynolog, or packages them into deb/rpm.

Common commands:

```bash
# Build the dyno and dynolog binaries
bash scripts/build.sh

# Build the deb package
bash scripts/build.sh -t deb

# Build the rpm package
bash scripts/build.sh -t rpm
```

### 4.2 Building and Installing `mindstudio_monitor`

#### Option 1: One-Click Installation

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

#### Option 2: Manually Building the whl

```bash
cd plugin
bash ./stub/build_stub.sh
python3 setup.py bdist_wheel
```

After the build is complete, the whl package is generated under `plugin/dist`. Then run:

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

### 5.1 Unit Tests

The repository provides unit test scripts by language. `scripts/run_ut.sh` runs the C++ UTs and then the Python UTs.

```bash
bash scripts/run_ut.sh
bash scripts/run_ut_cpp.sh
bash scripts/run_ut_python.sh
```

C++ UTs are built with CMake under `test/build_llt` and run with CTest. Python UTs run the tests under `test/ut/python` with unittest. For coverage usage, see the [Code Testing section of the contributing guide](../contributing/contributing_guide.md#code-testing).

### 5.2 System Tests

The repository provides `scripts/run_st.sh` as the system test entry point.

```bash
bash scripts/run_st.sh
```

The current system tests mainly run:

- `test/st/test_dynolog_build.py`

as well as other Python test files in the `test/st` directory that meet the rules.

### 5.3 Common Test Resources

The test directories are as follows:

| Directory | Description |
| --- | --- |
| `test/ut/plugin/ipc_monitor` | IPCMonitor unit tests |
| `test/st` | System tests |
| `test/st/gen_tls_certs.sh` | Test certificate generation script |

## 6. Documentation Synchronization

After feature development is complete, if the change affects user behavior, deployment methods, or interface definitions, the documentation must be updated accordingly.

| Change Type | Documents to Update |
| --- | --- |
| Installation, compilation, upgrade, uninstallation | `docs/en/install_guide/msmonitor_install_guide.md` |
| Quick start flow | `docs/en/quick_start/msmonitor_quick_start.md` |
| dynolog server | `docs/en/user_guide/dynolog_instruct.md` |
| dyno client | `docs/en/user_guide/dyno_instruct.md` |
| npu-monitor feature | `docs/en/user_guide/npumonitor_instruct.md` |
| nputrace feature | `docs/en/user_guide/nputrace_instruct.md` |
| Monitor APIs | `docs/en/advanced_features/monitor_feature.md` |
| API reference | `docs/en/advanced_features/mindstudio_monitor_api_reference.md` |

## 7. Recommended Submission Workflow

1. After feature development is complete, verify the local build first.
2. If dynolog patches or packaging logic are involved, verify `scripts/build.sh` at least once.
3. If plugin changes are involved, verify the whl build and installation at least once.
4. Run the relevant UTs at a minimum, and add STs when necessary.
5. If user-visible behavior changes, update the documentation and example commands accordingly.
