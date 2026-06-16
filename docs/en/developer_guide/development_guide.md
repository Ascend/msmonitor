# Development Guide

## 1. MindStudio Monitor development software

| Software name                          | Purpose                                              |
| -------------------------------------- | ---------------------------------------------------- |
| CLion (recommended) / VS Code          | Writing and Debugging`dynolog_npu`C++ code           |
| PyCharm (recommended)/VS Code          | Writing and Debugging`plugin`Python/CMake code under |
| Git                                    | Pull, Manage, and Commit Code                        |
| CMake / Ninja                          | Local Building and Debugging                         |
| Python Virtual Environment Tool (venv) | Isolating Python Dependencies                        |

## 2. Development environment configuration

| Software name | Version Requirements                             | Purpose                                              |
| ------------- | ------------------------------------------------ | ---------------------------------------------------- |
| gcc           | 8.5.0 and later                                  | Compilation`dynolog_npu`                             |
| Rust          | 1.81 and later                                   | Dependency Related to the Compilation of the Dynalog |
| protobuf      | 3.12 or later                                    | Dependency on dynolog and tensorboard                |
| Python        | Matches the target whl installation environment. | Compilation and Installation`mindstudio_monitor`     |
| pybind11      | Latest stable version                            | Constructed`plugin`Python extension                  |
| CMake         | Latest stable version                            | CMake build                                          |
| Ninja         | Latest stable version                            | Local build tool                                     |

### 2.1 Preparation for Dependency

According to the current installation guide, you are advised to prepare at least the following dependencies in the compilation environment:

```bash
#Debian / Ubuntu
sudo apt-get install -y cmake ninja-build
sudo apt install -y protobuf-compiler libprotobuf-dev

#Python
pip install pybind11 wheel protobuf
```

Rust recommends official use`rustup`Installation:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env
```

### 2.2 TLS Certificate Environment

If the TLS communication between the dyno CLI and dynolog daemon needs to be verified in the development and test scenarios, you need to prepare the certificate directories on the client and server. Catalog specifications can be found in[Installation Guide](../install_guide.md)    .

## 3. Development Procedure

### 3.1 Code Download

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

### 3.2 Project Structure Description

The current warehouse consists of the following modules:

| The Table of Contents | Description                                               |
| --------------------- | --------------------------------------------------------- |
| `dynolog_npu`         | Directory for storing the code of the dynolog_npu module. |
| `dynolog_npu/cli`     | dyno client source code                                   |
| `dynolog_npu/dynolog` | Source code of the dynolog server                         |
| `plugin`              | Python plug-in and IPCMonitor-related code                |
| `plugin/ipc_monitor`  | Core code of the IPCMonitor.                              |
| `plugin/IPCMonitor`   | Python Package Directory                                  |
| `scripts`             | Build, patch, UT, and ST scripts                          |
| `test/ut`             | Unit test                                                 |
| `test/st`             | System Test                                               |
| `third_party/dynolog` | Dynolog Submodule and Third-Party Dependency              |
| `docs/zh`             | Chinese document                                          |

### 3.3`dynolog_npu`development

`dynolog_npu`This module is responsible for the dyno CLI and dynolog daemon capabilities.

Focus on the following during development:

| Pathway                   | Description                               |
| ------------------------- | ----------------------------------------- |
| `dynolog_npu/cli/src`     | dyno CLI code                             |
| `dynolog_npu/dynolog/src` | Dynolog daemon code                       |
| `dynolog_npu/cmake`       | Build Configuration                       |
| `dynolog_npu/scripts/rpm` | Package related files in the RPM package. |

Application scenario:

1. The dyno subcommand is added or modified.
2. The request processing logic of the dylog daemon is extended.
3. The logic for reporting, collecting, and displaying data by the daemon process is adjusted.
4. Adjust the deb/rpm packing logic.

### 3.4`plugin`development

`plugin`Module provision`mindstudio_monitor`whl package, IPCMonitor, and MSPTI Monitor public capabilities.

Focus on the following during development:

| Pathway                 | Description                          |
| ----------------------- | ------------------------------------ |
| `plugin/setup.py`       | Whl construction entry               |
| `plugin/CMakeLists.txt` | Building the plug-in module CMake    |
| `plugin/bindings.cpp`   | Python Extended Binding Entry        |
| `plugin/ipc_monitor`    | IPCMonitor Core Code                 |
| `plugin/IPCMonitor`     | Python Package Content               |
| `plugin/stub`           | Scripts and codes for building stubs |

Application scenario:

1. The IPCMonitor capability is extended.
2. Add the Python interface or common module.
3. Adjust the pybind11 extension binding.
4. Adjust the contents of the whl package.

### 3.5 Common Development Scenarios

#### 3.5.1 Development`npu-monitor`

If the change involves`npu-monitor`\:

1. Pay special attention to the processing of the dyno CLI parameter.
2. Pay special attention to the monitoring request delivery and background collection logic of the dynolog daemon.
3. If data processing on the MSPTI side is involved, linkage is required.`plugin`Module.
4. Synchronous update`docs/zh/user_guide/npumonitor_instruct.md`.

#### 3.5.2 Development`nputrace`

If the change involves`nputrace`\:

1. Pay special attention to the dyno request parameters and the daemon triggering logic.
2. Check the association logic with the data collection on the profiler, CANN, and device sides.
3. If logs, output paths, offline parsing, or display are involved, the E2E process needs to be verified.
4. Synchronous update`docs/zh/user_guide/nputrace_instruct.md`.

#### 3.5.3 Developing Monitor APIs

If the modification involves Python APIs or public capabilities:

1. Prioritized attention`plugin/IPCMonitor`,`plugin/ipc_monitor`.
2. If the extended module is exposed, check it accordingly.`bindings.cpp`And to the`setup.py`.
3. Synchronous update`docs/zh/advanced_features/monitor_feature.md`And to the`docs/zh/advanced_features/mindstudio_monitor_api_reference.md`.

## 4. Build and Install

### 4.1 Building a dynolog

The repository provides unified build scripts.`scripts/build.sh`. The script:

1. Check the gcc and Rust versions.
2. Initialize and switch`third_party/dynolog`Submodule to the specified submission.
3. Generate and apply Ascend related patches.
4. Build dyno and dynolog, or pack them into deb / rpm.

The common commands are as follows:

```bash
#Building dyno and dyno binaries
bash scripts/build.sh

#Building a DEB Package
bash scripts/build.sh -t deb

#Creating an RPM Package
bash scripts/build.sh -t rpm
```

### 4.2 Build and Install`mindstudio_monitor`

#### Method 1: One-click installation

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

#### Method 2: Manually build the whl

```bash
cd plugin
bash ./stub/build_stub.sh
python3 setup.py bdist_wheel
```

After the build is complete,`plugin/dist`Generate the whl package and run the following command:

```bash
cd plugin/dist
pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
```

### 4.3 Local Run Verification 

After the build installation is complete, it is recommended to verify at least the following capabilities:

```bash
#Starting the Dynolog daemon
dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs

#Starting the npu-monitor
dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel

#Trigger nputrace.
dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
```

## 5. Test and Verification

### 5.1 Unit test

Courtesy of Warehouse`scripts/run_ut.sh`Used as the unit test entry.

```bash
#Run all UT builds and tests
bash scripts/run_ut.sh

#Run only plug-in-related tests.
bash scripts/run_ut.sh plugin
```

The current script will:

1. In the`test/build_llt`Run the CMake build command.
2. Default Build`ut`target.
3. Execution`test/ut/plugin/ipc_monitor`Executable test files under.

### 5.2 System Test

Courtesy of Warehouse`scripts/run_st.sh`Used as the system test entry.

```bash
bash scripts/run_st.sh
```

The current system test is mainly performed as follows:

 * `test/st/test_dynolog_build.py`

And also the`test/st`Other Python test files in the directory that meet the rules.

### 5.3 Common test resources

The test directory is as follows:

| The Table of Contents        | Description                             |
| ---------------------------- | --------------------------------------- |
| `test/ut/plugin/ipc_monitor` | IPCMonitor Unit Test                    |
| `test/st`                    | System test                             |
| `test/st/gen_tls_certs.sh`   | Test the certificate generation script. |

## 6. Document linkage update

After the function development is complete, if the modification affects user behavior, deployment mode, or interface definition, the document needs to be updated accordingly.

| Change type                                            | Documents to be updated synchronously                           |
| ------------------------------------------------------ | --------------------------------------------------------------- |
| Installation, compilation, upgrade, and uninstallation | `docs/zh/getting_started/install_guide.md`                      |
| Quick experience process                               | `docs/zh/getting_started/quick_start.md`                        |
| Dynolog server                                         | `docs/zh/user_guide/dynolog_instruct.md`                        |
| dyno client                                            | `docs/zh/user_guide/dyno_instruct.md`                           |
| NPU-monitor function                                   | `docs/zh/user_guide/npumonitor_instruct.md`                     |
| nputrace function                                      | `docs/zh/user_guide/nputrace_instruct.md`                       |
| Monitor API                                            | `docs/zh/advanced_features/monitor_feature.md`                  |
| API Reference                                          | `docs/zh/advanced_features/mindstudio_monitor_api_reference.md` |
| Version Release Information                            | `docs/zh/release_notes.md`                                      |

## 7. Submit Process Suggestions

1. After the function development is complete, complete the local build verification first.
2. If the Dynolog patch or packing logic is involved, verify the patch at least once.`scripts/build.sh`.
3. If plug-in changes are involved, verify the whl build and installation at least once.
4. Perform at least the relevant UTs and supplement STs as necessary.
5. Updated the document and example commands if user behavior changes are involved.
