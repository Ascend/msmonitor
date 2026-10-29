# msMonitor Installation Guide

## 1. Installation Notes

The msMonitor tool is supported only on Linux systems, compatible with both aarch64 and x86 CPU architectures. This tool supports three installation methods: [Online Installation](#21-online-installation), [Offline Installation](#22-offline-installation), and [Source Installation](#23-source-installation). Please select the most suitable option based on your actual environment.

## 2. Installation Methods

### 2.1 Online Installation

If your device has internet access, you can automatically download and install the tool with a single command. Visit the Ascend Community MindStudio [download](https://www.hiascend.com/en/developer/software/mindstudio/download) page, select the corresponding CANN version, and choose **Online** installation method. The system will guide you through the subsequent steps.

### 2.2 Offline Installation

For devices in environments without external network access, such as enterprise intranets, first download the complete offline installation package on a machine with internet access, then transfer it to the target device for installation. Visit the Ascend Community MindStudio [download](https://www.hiascend.com/en/developer/software/mindstudio/download) page, select the corresponding CANN version, and choose **Offline** installation method to obtain the corresponding installation package and operation guide.

### 2.3 Source Installation

If you need the features of the latest code, or want to modify the source code to enhance functionality, you can download the code from this repository, compile and package the tool yourself, and complete the installation.

#### 2.3.1 Environment Preparation

Source code compilation uses the MindStudio standard build environment.

- For daily development or when using a released image, see the *[MindStudio Tool Development Environment Setup Guide](https://gitcode.com/Ascend/msot/blob/master/docs/en/common/dev_env_setup.md)*.
- To reproduce the environment from a base OS, or to perform source build verification or unit test verification, you must refer to the *[MindStudio Tool Unified Build Image Creation Guide](https://gitcode.com/Ascend/msot/blob/master/docs/en/common/docker_image_build_guide.md)* and build the environment image on-site from the openEuler base image.

All source compilation and unit test commands in this document are run in the container of the image specified above or built on-site. The CANN package version, GCC version, and Python version are subject to the unified image creation guide; this repository does not maintain them separately.

After the image is built, you must use the `ctr_in.py` command provided in Chapter 7 of the unified image creation guide to start and enter the container in an interactive terminal. Do not use plain `docker run` to create a container, and do not use `docker exec <container_name> bash -c '<command>'` as a substitute for an interactive environment; otherwise, Python, GCC, and CANN environment initialization may be skipped.

After entering the interactive container shell opened by `ctr_in.py`, run the following command to clone this repository:

```bash
cd ~
git clone https://gitcode.com/Ascend/msmonitor.git -b master
```

> Optional: To install OpenSSL (RPC TLS authentication) and generate a certificate key, see [Section 5](#5-installing-openssl-rpc-tls-authentication-and-generating-certificate-keys).

#### 2.3.2 Compiling and Installing dynolog

Stay in the same interactive container shell opened by `ctr_in.py`, and run the following command in the repository root directory to automatically download dependencies and build:

```bash
cd ~/msmonitor
python3 build.py -e dynolog=true
```

After a successful build, the installation package is generated in the `artifacts/` directory.

To install dynolog, choose the method based on your system:

```bash
cd ~/msmonitor/artifacts
# Debian/Ubuntu and similar systems
dpkg -i --force-overwrite dynolog*.deb --ignore-depends
# RedHat/Fedora/openSUSE and similar systems
rpm -ivh dynolog*.rpm --nodeps
```

Verify that dynolog is installed successfully:

```bash
dyno --help
dynolog --help
```

If the commands do not report errors and the help information is displayed, the installation is successful.

#### 2.3.3 Compiling and Installing mindstudio_monitor

The mindstudio_monitor `.whl` package provides common capabilities such as IPCMonitor and MsptiMonitor. This `.whl` package must be installed before using the nputrace and npu-monitor features.

Stay in the same interactive container shell opened by `ctr_in.py`, and run the following command in the repository root directory to automatically download dependencies and build:

```bash
cd ~/msmonitor
python3 build.py -e whl=true
```

After a successful build, the installation package is generated in the `artifacts/` directory.

Installation method:

```bash
cd ~/msmonitor/artifacts
pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
```

Verify the installation:

```bash
python3 -c "import msmonitor; print('All is OK')"
```

If the command does not report errors and prints 'All is OK', the installation is successful.

## 3. Uninstallation

You can perform uninstallation by following these steps:

1. Download the script.

   ```bash
   curl -O https://inst.obs.cn-north-4.myhuaweicloud.com/26.1.0/ms_install.py
   ```

   > [!NOTE]
   >
   > - An internet connection is required for downloading. If the environment does not allow internet access or is offline, download the script in an environment with internet access first and then copy it to the target device.
   > - If the command does not respond or errors such as connection failure or SSL certificate errors occur, see [FAQs](https://www.hiascend.com/developer/blog/details/02176213671719317003).

2. Perform the uninstallation.

   ```bash
   python ms_install.py uninstall {tools_name}
   ```

   Where `{tools_name}` is the name of the tool to be uninstalled. You can query it with the `python ms_install.py help` command; the tool name is displayed under the `Available Tools` field in the printed information.

   If the uninstallation is successful, the following information is printed:

   ```text
   Successfully uninstalled 1 tool ({tools_name})
   ```

## 4. Upgrade

Upgrading means "uninstalling first and then installing". Run the install command directly. The tool automatically uninstalls the old version and guides you through the overwrite installation.

You can run the `dyno --version` command to view the version information of the current environment, and then select the version to upgrade to. Pay attention to version compatibility when upgrading. See [Release Notes](https://gitcode.com/Ascend/release-management/blob/master/MindStudio/master/release_notes_en.md).

## 5. Installing OpenSSL (RPC TLS Authentication) and Generating Certificate Keys

The RPC communication between the dyno CLI and the dynolog daemon is encrypted using TLS certificate keys. When starting the dyno and dynolog binaries, you can specify the path where the certificate keys are stored. The directory must have the following structure and names.

Users should use key generation and storage mechanisms that match their own requirements, and ensure key security and confidentiality. Currently, only the RSA-SHA256 and RSA-SHA512 certificate signature algorithms are supported.

Server certificate directory structure:

```text
ssl_certs
├── ca.crt (Root CA certificate, used to verify the validity of other certificates. Required)
├── server.crt (Server certificate, used to prove the server identity to clients. Required)
├── server.key (Server private key file, used in pair with server.crt. Encryption is supported. Required)
└── ca.crl (Certificate revocation list, containing information about revoked certificates. Optional)
```

Client certificate directory structure:

```text
ssl_certs
├── ca.crt (Root CA certificate, used to verify the validity of other certificates. Required)
├── client.crt (Client certificate, used to prove the client identity to the server. Required)
├── client.key (Client private key file, used in pair with client.crt. Encryption is supported. Required)
└── ca.crl (Certificate revocation list, containing information about revoked certificates. Optional)
```
