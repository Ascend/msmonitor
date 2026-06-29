# msMonitor Tool Installation Guide

<!-- md-trans-meta sourceCommit=unknown translatedAt=2026-06-25T02:21:37.101Z pushedAt=2026-06-25T03:31:07.456Z -->

## 1. Installation Notes

This tool supports three installation methods: [Online Installation](#21-online-installation), [Offline Installation](#22-offline-installation), and [Source Installation](#23-source-installation). Please select the most suitable option based on your actual environment.

## 2. Installation Methods

### 2.1 Online Installation

If your device has internet access, you can automatically download and install the tool with a single command. Visit the Ascend Community MindStudio [download](https://www.hiascend.com/en/developer/software/mindstudio/download) page, select the corresponding CANN version, and choose **Online** installation method. The system will guide you through the subsequent steps.

### 2.2 Offline Installation

For devices in environments without external network access, such as enterprise intranets, first download the complete offline installation package on a machine with internet access, then transfer it to the target device for installation. Visit the Ascend Community MindStudio [download](https://www.hiascend.com/en/developer/software/mindstudio/download) page, select the corresponding CANN version, and choose **Offline** installation method to obtain the corresponding installation package and operation guide.

### 2.3 Source Installation

#### 2.3.1 Installing Dependencies

The compilation dependencies for dynolog are as follows. Ensure that the following dependencies are installed. Users are responsible for ensuring the security of any third-party dependencies installed manually and should avoid installing versions with known security vulnerabilities.

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

   After installation, run the `rustc --version` command to check the version number and confirm that the installation was successful.

2. Install Ninja.

   ```bash
   # debian
   sudo apt-get install -y cmake ninja-build
   
   # centos
   sudo yum install -y cmake ninja
   ```

   After installation, run the `ninja --version` command to check the version number and confirm that the installation is successful.

3. Install protobuf (a third-party dependency of tensorboard_logger, used for interfacing with tensorboard for display).

   ```bash
   # debian
   sudo apt install -y protobuf-compiler libprotobuf-dev

   # centos
   sudo yum install -y protobuf protobuf-devel protobuf-compiler

   # Python
   pip install protobuf
   ```

4. (Optional) Install openssl (RPC TLS authentication) and generate a certificate key.

   > [!NOTE]
   >
   > If you do not need to use TLS certificate key encryption, this step can be skipped.

   ```bash
   # debian
   sudo apt-get install -y openssl
   
   # centos
   sudo yum install -y openssl
   ```

   The RPC communication between the dyno CLI and the dynolog daemon is encrypted using a TLS certificate key. When starting the dyno and dynolog binaries, you can specify the directory where the certificate key is stored. The directory must meet the following structure and naming requirements.

   Users should use key generation and storage mechanisms that meet their own requirements, and ensure key security and confidentiality. Currently, only RSA-SHA256 and RSA-SHA512 certificate signature algorithms are supported.

   Server certificate directory structure:

   ```ColdFusion
   ssl_certs
   ├── ca.crt (Root CA certificate for validating peer certificates. Required)
   ├── server.crt (Server certificate to authenticate the server to clients. Required)
   ├── server.key (Private key paired with server.crt. May be encrypted. Required)
   └── ca.crl (Certificate revocation list (CRL) containing revoked certificates. Optional)
   ```

   Client certificate directory structure:

   ```ColdFusion
   ssl_certs
   ├── ca.crt (Root CA certificate for validating peer certificates. Required)
   ├── client.crt (Client certificate to authenticate the client to server. Required)
   ├── client.key (Private key paired with client.crt. May be encrypted. Required)
   └── ca.crl (Certificate revocation list (CRL) containing revoked certificates. Optional)
   ```

#### 2.3.2 Downloading the Source Code

Download the source code and enter the source code directory.

```bash
git clone https://gitcode.com/Ascend/msmonitor.git -b 26.0.0
cd msmonitor
```

#### 2.3.3 Compiling and Installing dynolog

1. Compile dynolog.

   By default, the compilation generates the dyno and dynolog binary files. The `-t` option can be used to package the binary files into a `.deb` or `.rpm` package.

   ```bash
   # # Compile the .deb package. Currently supports amd64 and aarch64 platforms, defaulting to amd64. To compile for the aarch64 platform, change Architecture to arm64 in the third_party/dynolog/scripts/debian/control file
   bash scripts/build.sh -t deb

   # # Compile the .rpm package. Currently only supports the amd64 platform
   bash scripts/build.sh -t rpm

   # Compile dyno and dynolog binary executable files
   bash scripts/build.sh
   ```

2. Install dynolog.

   The following installation methods are available. Choose one based on your server operating system:

   - Method 1: Install using the `.deb` package (applicable to Debian/Ubuntu and similar systems).

     ```bash
     dpkg -i --force-overwrite dynolog*.deb
     ```

   - Method 2: Install using the `.rpm` package (applicable to RedHat/Fedora/openSUSE and similar systems).

     ```bash
     rpm -ivh dynolog*.rpm --nodeps
     ```

#### 2.3.4 Compiling and Installing mindstudio_monitor

The mindstudio_monitor `.whl` package provides common capabilities such as IPCMonitor and MsptiMonitor. This `.whl` package must be installed before using the nputrace and npu-monitor features.

##### 2.3.4.1 One-Click Installation via Shell Script

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

The following information is printed upon successful installation:

```ColdFusion
Successfully installed mindstudio_monitor-<version> pybind11-<version>
```

##### 2.3.4.2 Manual Installation

1. Install dependencies.

   ```bash
   pip install wheel
   pip install pybind11
   ```

2. Compile the mindstudio_monitor `.whl` package.

   ```bash
   cd ./plugin
   bash ./stub/build_stub.sh
   python3 setup.py bdist_wheel
   ```

   After compilation, the mindstudio_monitor `.whl` package is generated in the `msmonitor/plugin/dist` directory.

3. Install the mindstudio_monitor `.whl` package.

   ```bash
   cd ./plugin/dist
   pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
   ```

   If the installation is successful, the following information is printed:

   ```ColdFusion
   Successfully installed mindstudio_monitor-<version> pybind11-<version> xlsxwriter-<version>
   ```

## 3. Installation Verification

After the installation is complete, run the following commands to verify whether the tools are installed successfully:

```bash
dyno --help
dynolog --help
```

If the output does not report an error and the help information is displayed, the installation is successful.

If `dyno --help` or `dynolog --help` indicates that the command does not exist, confirm that the current terminal is using the Python environment where `msMonitor` is installed.

## 4. Uninstallation

You can perform uninstallation  by following these steps:

1. Download the script.

   ```bash
   curl -O https://inst.obs.cn-north-4.myhuaweicloud.com/26.0.0/ms_install.py
   ```

   > [!NOTE]
   >
   > - An internet connection is required for downloading. If the environment does not allow internet access or is offline, download the script in an environment with internet access first and then copy it to the target device.
   > - If the command does not respond or errors such as connection failure or SSL certificate errors occur, see [FAQs](https://www.hiascend.com/developer/blog/details/02176213671719317003).

2. Perform the uninstallation.

   ```bash
   python ms_install.py uninstall {tools_name}
   ```

   Where `{tools_name}` is configured as the name of the tool to be uninstalled. You can query it by running the `python ms_install.py help` command. The tool name is displayed under the `Available Tools` field in the printed information.

If the uninstallation is successful, the following information is printed:

   ```ColdFusion
   Successfully uninstalled 1 tool ({tools_name})
   ```

## 5. Upgrade

Upgrading means "uninstalling first and then installing". Run the install command directly. The tool will automatically remove any older version and guide you through a clean reinstallation.

You can run the `dyno --version` command to view the version information of the current environment, and then select the version to upgrade to. When upgrading, pay attention to the version compatibility. See [Release Notes](https://gitcode.com/Ascend/release-management/blob/master/MindStudio/26.0.0/release_notes_en.md).

## 6. Logs

Users can configure the `MSMONITOR_LOG_PATH` environment variable to specify a custom log file path. The default path is `msmonitor_log` in the current directory.

```bash
export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
```

`/tmp/msmonitor_log` is the custom log file path.
