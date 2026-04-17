# msMonitor Installation Guide

## Installation Description

This document describes how to install msMonitor. Currently, installation via software package and compilation from source are supported.

## Installation via Software Package

You are advised to install msMonitor by installing the software package. The procedure is as follows:

1. Select the software package based on [Version Mapping](../../README.md#Version Mapping) and download it to the Linux installation environment.

2. Verify the package integrity.

   Go to the directory containing the .zip package and run the following command:

   ```bash
   sha256sum {name}.zip
   ```

   *{name}* indicates the name of the .zip package.

   If the command output displays a checksum that matches the corresponding .zip package version, the correct tool installation package has been downloaded. The following is an example:

   ```bash
   2c675ae346dfc1c70f5e9c7103d6f8c7e53be00dca28ed5f9cc577ac59e4bc44 aarch64_8.3.0.zip
   ```

3. Install the msmonitor_plugin .whl package.

   ```bash
   # Decompress the package.
   mkdir x86
   unzip x86_8.3.0.zip -d x86

   # Go to the directory of the depressed installation package.
   cd x86

   # Install the .whl package that matches the Python version in the current environment.
   pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
   ```

   If the installation is successful, the following information is displayed:

   ```ColdFusion
   Successfully installed mindstudio_monitor-<version> pybind11-<version>
   ```

4. Install dynolog.

   Select an installation method based on the server OS.

   - Method 1: Use the .deb software package (applicable to Debian/Ubuntu).

     ```bash
     dpkg -i --force-overwrite dynolog*.deb
     ```

   - Method 2: Use the .rpm software package (applicable to Red Hat/Fedora/openSUSE).

     ```bash
     rpm -ivh dynolog*.rpm --nodeps
     ```

## Compilation and Installation

### Installing Dependencies

The following table lists the dependencies required for compiling dynolog. Ensure that the dependencies have been installed. You need to ensure the security of third-party dependencies that you manually install. Do not install versions with security vulnerabilities.

| Language | Toolchain        |
| -------- | ---------------- |
| C++      | gcc >= 8.5.0     |
| Rust     | Rust >= 1.81     |
| protobuf | protobuf >= 3.12 |

1. Install Rust.

   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   source $HOME/.cargo/env
   ```

   After the installation is complete, run the `rustc --version` command to check the version number and ensure that the installation is successful.

2. Install Ninja.

   ```bash
   # debian
   sudo apt-get install -y cmake ninja-build

   # centos
   sudo yum install -y cmake ninja
   ```

   After the installation is complete, run the `ninja --version` command to check the version number and ensure that the installation is successful.

3. Install Protobuf (third-party dependency of tensorboard_logger, which is used to connect to TensorBoard for display).

   ```bash
   # debian
   sudo apt install -y protobuf-compiler libprotobuf-dev

   # centos
   sudo yum install -y protobuf protobuf-devel protobuf-compiler

   # Python
   pip install protobuf
   ```

4. (Optional) Install OpenSSL (RPC TLS authentication) and generate a certificate key.

   > [!NOTE]NOTE
   >
   > Skip this step if you do not need to use the TLS certificate key for encryption.

   ```bash
   # debian
   sudo apt-get install -y openssl

   # centos
   sudo yum install -y openssl
   ```

   The RPC communication between the dyno CLI and dynolog daemon is encrypted using the TLS certificate key. When starting the dyno and dynolog binaries, you can specify the path for storing the certificate key. The path must meet the following structure and name requirements.

   You should use a key generation and storage mechanism that meets your requirements and ensure the security and confidentiality of the key. Currently, only the RSA-SHA256 and RSA-SHA512 certificate signing algorithms are supported.

   Server certificate directory structure:

   ```ColdFusion
   server_certs
   ├── ca.crt (root certificate, which is used to verify the validity of other certificates. This certificate is mandatory.)
   ├── server.crt (server certificate, which is used to prove the server identity to the client. This certificate is mandatory.)
   ├── server.key (server private key file, which is used together with server.crt. This file supports encryption and is mandatory.)
   └── ca.crl (certificate revocation list, which contains information about revoked certificates. This certificate is optional.)
   ```

   Client certificate directory structure:

   ```ColdFusion
   client_certs
   ├── ca.crt (root certificate, which is used to verify the validity of other certificates. This certificate is mandatory.)
   ├── client.crt (client certificate, which is used to prove the client identity to the server. This certificate is mandatory.)
   ├── client.key (client private key file, which is used together with client.crt. This file supports encryption and is mandatory.)
   └── ca.crl (certificate revocation list, which contains information about revoked certificates. This certificate is optional.)
   ```

### Downloading the Source Code

Download the source code and go to the source code directory.

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

### Compiling and Installing dynolog

1. Compile dynolog.

   By default, the dyno and dynolog binary files are generated after compilation. `-t` can be used to pack the binary files into a .deb or .rpm package.

   ```bash
   # Compile the .deb package. Currently, the AMD64 and AArch64 platforms are supported. The default platform is AMD64. To compile the AArch64 platform, change **Architecture** in the **third_party/dynolog/scripts/debian/control** file to **arm64**.
   bash scripts/build.sh -t deb

   # Compile the .rpm package. Currently, only the AMD64 platform is supported.
   bash scripts/build.sh -t rpm

   # Compile the dyno and dynolog binary executable files.
   bash scripts/build.sh
   ```

2. Install dynolog.

   Select an installation method based on the server OS.

   - Method 1: Use the .deb software package (applicable to Debian/Ubuntu).

     ```bash
     dpkg -i --force-overwrite dynolog*.deb
     ```

   - Method 2: Use the .rpm software package (applicable to Red Hat/Fedora/openSUSE).

     ```bash
     rpm -ivh dynolog*.rpm --nodeps
     ```

### Compiling and Installing mindstudio_monitor

The mindstudio_monitor .whl package provides common capabilities such as IPCMonitor and MsptiMonitor. Before using the nputrace and npu-monitor functions, you must install the .whl package.

#### Installation Using Shell Scripts

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

If the installation is successful, the following information is displayed:

```ColdFusion
Successfully installed mindstudio_monitor-<version> pybind11-<version>
```

#### Manual Installation

1. Install the dependencies.

   ```bash
   pip install wheel
   pip install pybind11
   ```

2. Compile the mindstudio_monitor .whl package.

   ```bash
   cd ./plugin
   bash ./stub/build_stub.sh
   python3 setup.py bdist_wheel
   ```

   After the compilation is complete, the mindstudio_monitor .whl package is generated in the **msmonitor/plugin/dist** directory.

3. Install the mindstudio_monitor .whl package.

   ```bash
   cd ./plugin/dist
   pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
   ```

   If the installation is successful, the following information is displayed:

   ```ColdFusion
   Successfully installed mindstudio_monitor-<version> pybind11-<version> xlsxwriter-<version>
   ```

## Uninstallation

**Uninstalling dynolog**

You can run the `which` command to search for the locations of the dyno and dynolog binary executable files and manually delete the two files.

**Uninstalling mindstudio_monitor**

```bash
pip uninstall mindstudio_monitor
```

## Upgrade

msMonitor cannot be directly upgraded. You need to [uninstall](#uninstallation) msMonitor and then [install](#installation-description) a later version.

## Logs

You can configure the `MSMONITOR_LOG_PATH` environment variable to specify a custom log file path. The default path is `msmonitor_log` in the current directory.

```bash
export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
```

 `/tmp/msmonitor_log` is the custom log file path.
