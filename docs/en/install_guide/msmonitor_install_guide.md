# msMonitor Installation Guide

<br>

## 1. Installation Notes

The msMonitor tool is supported only on Linux systems, compatible with both aarch64 and x86 CPU architectures. This tool supports three installation methods: [Online Installation](#21-online-installation), [Offline Installation](#22-offline-installation), and [Source Installation](#23-source-installation). Please select the most suitable option based on your actual environment.

## 2. Installation Methods

### 2.1 Online Installation

If your device has internet access, you can automatically download and install the tool with a single command. Visit the Ascend Community MindStudio [download](https://www.hiascend.com/en/developer/software/mindstudio/download) page, select the corresponding CANN version, and choose **Online** installation method. The system will guide you through the subsequent steps.

### 2.2 Offline Installation

For devices in environments without external network access, such as enterprise intranets, first download the complete offline installation package on a machine with internet access, then transfer it to the target device for installation. Visit the Ascend Community MindStudio [download](https://www.hiascend.com/en/developer/software/mindstudio/download) page, select the corresponding CANN version, and choose **Offline** installation method to obtain the corresponding installation package and operation guide.

### 2.3 Source Installation

If you need the features of the latest code, or want to modify the source code to enhance functionality, you can download the code of this repository, compile and package the tool yourself, and complete the installation.

#### 2.3.1 Environment Preparation

Source code compilation uniformly uses the MindStudio standard build environment.

- For daily development or when using a released image, see *[MindStudio Tool Development Environment Installation Guide](https://gitcode.com/Ascend/msot/blob/26.2.0/docs/en/common/dev_env_setup.md)*.
- When you need to reproduce the environment from the base OS, perform source code build verification, or perform unit test verification, you must refer to the *[MindStudio Unified Build Image Creation Guide](https://gitcode.com/Ascend/msot/blob/26.2.0/docs/en/common/docker_image_build_guide.md)* and build the environment image on site from the openEuler base image.

The source code compilation and unit test commands in the rest of this document are all executed in the container of the specified image above or the environment image built on site. The CANN software package version, GCC version, and Python version are subject to the unified image creation guide and are not maintained repeatedly in this repository.

After the image is built, you must use the `ctr_in.py` command given in Chapter 7 of the unified image creation guide to start and enter the container in an interactive terminal. Do not use a normal `docker run` to create a container, and do not use `docker exec <container name> bash -c '<command>'` to replace the interactive environment; otherwise, the initialization of the Python, GCC, and CANN environments may be skipped.

After entering the interactive container shell opened by `ctr_in.py`, run the following command to clone this repository:

```bash
cd ~
git clone https://gitcode.com/Ascend/msmonitor.git -b 26.2.0
```

> Optional: If you need to install openssl (RPC TLS authentication) and generate a certificate key, see [Section 5](#5-installing-openssl-rpc-tls-authentication-and-generating-certificate-keys).

#### 2.3.2 Compiling and Installing dynolog

Staying in the same interactive container shell opened by `ctr_in.py`, run the following command in the repository root directory to automatically complete dependency download and build:

```bash
cd ~/msmonitor
python3 build.py -e dynolog=true
```

After a successful build, the installation package is generated in the `artifacts/` directory.

To install dynolog, select the corresponding method based on the system:

```bash
cd ~/msmonitor/artifacts
# Systems such as Debian/Ubuntu
dpkg -i --force-overwrite dynolog*.deb --ignore-depends
# Systems such as RedHat/Fedora/openSUSE
rpm -ivh dynolog*.rpm --nodeps
```

Verify whether dynolog is installed successfully:

```bash
dyno --help
dynolog --help
```

If the output does not report an error and the help information is displayed, the installation is successful.

#### 2.3.3 Compiling and Installing mindstudio_monitor

The mindstudio_monitor whl package provides common capabilities such as IPCMonitor and MsptiMonitor. This whl package must be installed before using the nputrace and npu-monitor features.

Staying in the same interactive container shell opened by `ctr_in.py`, run the following command in the repository root directory to automatically complete dependency download and build:

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

Verify whether the installation is successful:

```bash
python3 -c "import msmonitor; print('All is OK')"
```

If the output has no error and displays 'All is OK', the installation is successful.

## 3. Uninstallation

You can perform uninstallation by following these steps:

1. Download the script.

   ```bash
   curl -O https://inst.obs.cn-north-4.myhuaweicloud.com/26.2.0/ms_install.py
   ```

   > [!NOTE]
   >
   > - A networked environment is required for the download. If the environment does not allow networking or is offline, download the script in a networked environment first and then copy it to the target device.
   > - If the command does not respond or errors such as connection failure or SSL certificate errors occur, see [FAQs](https://www.hiascend.com/developer/blog/details/02176213671719317003).

2. Perform the uninstallation.

   ```bash
   python ms_install.py uninstall {tools_name}
   ```

   Where `{tools_name}` is configured as the name of the tool to be uninstalled. You can query it by running the `python ms_install.py help` command. The tool name is displayed under the `Available Tools` field in the printed information.

If the uninstallation is successful, the following information is printed:

   ```text
   Successfully uninstalled 1 tool ({tools_name})
   ```

## 4. Upgrade

Upgrading means "uninstalling first and then installing". Run the install command directly. The tool will automatically remove any older version and guide you through a clean reinstallation.

You can use the `dyno --version` command to view the version information of the current environment and then select the version to upgrade to. When upgrading the version, pay attention to the version compatibility relationship. See *[Release Notes](https://gitcode.com/Ascend/release-management/blob/master/MindStudio/26.2.0/release_notes_en.md)*.

## 5. Installing openssl (RPC TLS Authentication) and Generating Certificate Keys

The RPC communication between the dyno CLI and the dynolog daemon is encrypted using TLS certificate keys. When starting the dyno and dynolog binaries, you can specify the path where the certificate keys are stored. The structure and names under the path must meet the following requirements.

Users should use a key generation and storage mechanism that matches their own needs and ensure the security and confidentiality of the keys. Currently, only the RSA-SHA256 and RSA-SHA512 certificate signature algorithms are supported.

Server certificate directory structure:

```text
ssl_certs
├── ca.crt (Root certificate, used to verify the legitimacy of other certificates, mandatory)
├── server.crt (Server-side certificate, used to prove the server identity to the client, mandatory)
├── server.key (Server-side private key file, used in pair with server.crt, encryption supported, mandatory)
└── ca.crl (Certificate revocation list, containing information about revoked certificates, optional)
```

Client certificate directory structure:

```text
ssl_certs
├── ca.crt (Root certificate, used to verify the legitimacy of other certificates, mandatory)
├── client.crt (Client certificate, used to prove the client identity to the server, mandatory)
├── client.key (Client-side private key file, used in pair with client.crt, encryption supported, mandatory)
└── ca.crl (Certificate revocation list, containing information about revoked certificates, optional)
```
