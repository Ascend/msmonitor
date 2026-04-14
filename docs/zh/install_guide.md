# msMonitor工具安装指南

msMonitor工具的安装方式包括[下载软件包安装](#下载软件包安装)和[编译软件包安装](#编译软件包安装)。

## 下载软件包安装

推荐使用下载软件包安装，步骤如下：

1. 根据[版本配套说明](../../README.md#版本配套说明)选择对应软件包并下载到Linux安装环境。

2. 校验包完整性。

   进入zip包所在目录，执行如下命令。

   ```bash
   sha256sum {name}.zip
   ```

   {name}为zip包名称。

   若回显呈现对应版本zip包一致的**校验码**，则表示下载了正确的性能工具zip安装包。示例如下：

   ```bash
   2c675ae346dfc1c70f5e9c7103d6f8c7e53be00dca28ed5f9cc577ac59e4bc44 aarch64_8.3.0.zip
   ```

3. 安装msmonitor_plugin whl包。

   ```bash
   # 解压压缩包
   mkdir x86
   unzip x86_8.3.0.zip -d x86

   # 进入解压后的目录
   cd x86

   # 安装whl包，须选择与当前环境Python版本一致的whl包
   pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
   ```

   安装成功打印如下信息：

   ```ColdFusion
   Successfully installed mindstudio_monitor-<version> pybind11-<version>
   ```

4. 安装dynolog。

   有以下安装方式可供选择，根据用户服务器系统自行选择：

   - 方式一：使用deb软件包安装（适用于Debian/Ubuntu等系统）。

     ```bash
     dpkg -i --force-overwrite dynolog*.deb
     ```

   - 方式二：使用rpm软件包安装（适用于RedHat/Fedora/openSUSE等系统）。

     ```bash
     rpm -ivh dynolog*.rpm --nodeps
     ```

## 编译软件包安装

### 安装依赖

dynolog的编译依赖如下，请确保已安装以下依赖，用户手动安装的第三方依赖由用户自行确保安全性，避免安装存在安全漏洞的版本。

| Language | Toolchain        |
| -------- | ---------------- |
| C++      | gcc >= 8.5.0     |
| Rust     | Rust >= 1.81     |
| protobuf | protobuf >= 3.12 |

1. 安装rust

   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   source $HOME/.cargo/env
   ```

   安装完成后通过rustc --version命令查看版本号并确认安装成功。

2. 安装ninja

   ```bash
   # debian
   sudo apt-get install -y cmake ninja-build

   # centos
   sudo yum install -y cmake ninja
   ```

   安装完成后通过ninja --version命令查看版本号并确认安装成功。

3. 安装protobuf（tensorboard_logger第三方依赖，用于对接tensorboard展示）

   ```bash
   # debian
   sudo apt install -y protobuf-compiler libprotobuf-dev

   # centos
   sudo yum install -y protobuf protobuf-devel protobuf-compiler

   # Python
   pip install protobuf
   ```

4. （可选）安装openssl（RPC TLS认证）& 生成证书密钥

   > [!NOTE] 说明
   >
   > 如果不需要使用TLS证书密钥加密，该步骤可跳过。

   ```bash
   # debian
   sudo apt-get install -y openssl
   
   # centos
   sudo yum install -y openssl
   ```

   dyno CLI与dynolog daemon之间的RPC通信使用TLS证书密钥加密，在启动dyno和dynolog二进制时可以指定证书密钥存放的路径，路径下需要满足如下结构和名称。

   用户应使用与自己需求相符的密钥生成和存储机制，并保证密钥安全性与机密性。当前仅支持RSA-SHA256和RSA-SHA512两种证书签名算法。

   服务端证书目录结构：

   ```ColdFusion
   server_certs
   ├── ca.crt (根证书，用于验证其他证书的合法性，必选)
   ├── server.crt (服务器端的证书，用于向客户端证明服务器身份，必选)
   ├── server.key (服务器端的私钥文件，与server.crt配对使用，支持加密，必选)
   └── ca.crl (证书吊销列表，包含已被吊销的证书信息，可选)
   ```

   客户端证书目录结构：

   ```ColdFusion
   client_certs
   ├── ca.crt (根证书，用于验证其他证书的合法性，必选)
   ├── client.crt (客户端证书，用于向服务器证明客户端身份，必选)
   ├── client.key (客户端的私钥文件，与client.crt配对使用，支持加密，必选)
   └── ca.crl (证书吊销列表，包含已被吊销的证书信息，可选)
   ```

### 下载源码

下载源码并进入源码目录。

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

### 编译并安装dynolog

1. 编译dynolog。

   默认编译生成dyno和dynolog二进制文件，-t参数可以支持将二进制文件打包成deb包或rpm包。

   ```bash
   # 编译deb包, 当前支持amd64和aarch64平台, 默认为amd64, 编译aarch64平台需要修改third_party/dynolog/scripts/debian/control文件中的Architecture改为arm64
   bash scripts/build.sh -t deb

   # 编译rpm包, 当前只支持amd64平台
   bash scripts/build.sh -t rpm

   # 编译dyno和dynolog二进制可执行文件
   bash scripts/build.sh
   ```

2. 安装dynolog。

   有以下安装方式可供选择，根据用户服务器系统自行选择：

   - 方式一：使用deb软件包安装（适用于Debian/Ubuntu等系统）。

     ```bash
     dpkg -i --force-overwrite dynolog*.deb
     ```

   - 方式二：使用rpm软件包安装（适用于RedHat/Fedora/openSUSE等系统）。

     ```bash
     rpm -ivh dynolog*.rpm --nodeps
     ```

### 编译并安装mindstudio_monitor

mindstudio_monitor whl包提供IPCMonitor，MsptiMonitor等公共能力，使用nputrace和npu-monitor功能前必须安装该whl包。

#### shell脚本一键安装

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

安装成功打印如下信息：

```ColdFusion
Successfully installed mindstudio_monitor-<version> pybind11-<version>
```

#### 手动安装

1. 安装依赖。

   ```bash
   pip install wheel
   pip install pybind11
   ```

2. 编译mindstudio_monitor whl包。

   ```bash
   cd ./plugin
   bash ./stub/build_stub.sh
   python3 setup.py bdist_wheel
   ```

   编译完成后在msmonitor/plugin/dist目录下生成mindstudio_monitor whl包。

3. 安装mindstudio_monitor whl包。

   ```bash
   cd ./plugin/dist
   pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
   ```

   安装成功打印如下信息：

   ```ColdFusion
   Successfully installed mindstudio_monitor-<version> pybind11-<version> xlsxwriter-<version>
   ```

## 卸载

**卸载dynolog**

可通过which命令查找dyno和dynolog二进制可执行文件所在位置，手动删除这两个文件即可。

**卸载mindstudio_monitor**

```bash
pip uninstall mindstudio_monitor
```

## 升级

msMonitor不支持直接升级，需要先完成[卸载](#卸载)后再重新[安装](#msmonitor工具安装指南)。

## 日志

用户可以通过配置MSMONITOR_LOG_PATH环境变量，指定到自定义的日志文件路径，默认路径为当前目录下的msmonitor_log

```bash
export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
```

 /tmp/msmonitor_log为自定义日志文件路径。
