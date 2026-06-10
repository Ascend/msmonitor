# msMonitor工具安装指南

## 1. 安装说明

msMonitor工具仅支持在Linux系统下使用，兼容aarch64和x86 CPU架构，支持[在线安装](#21-在线安装)、[离线安装](#22-离线安装)、[源码安装](#23-源码安装)三种安装方式，请根据您的实际环境选择最合适的方案。

## 2. 安装方式

### 2.1 在线安装

若您的设备具备互联网访问能力，可通过一条命令自动完成工具的下载与安装。请参见昇腾社区MindStudio[下载](https://www.hiascend.com/developer/software/mindstudio/download)页面，选择对应的CANN版本，并在安装方式中选择“在线安装”，系统将引导您完成后续操作。

### 2.2 离线安装

对处于企业内网等无外网环境的设备，请先在可联网的机器上下载完整的离线安装包，再将其传输至目标设备进行安装。请参见昇腾社区MindStudio[下载](https://www.hiascend.com/developer/software/mindstudio/download)页面，选择对应的CANN版本，并在安装方式中选择“离线安装”，获取对应的安装包及操作指引。

### 2.3 源码安装

#### 2.3.1 安装依赖

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

3. 安装protobuf（tensorboard_logger第三方依赖，用于对接TensorBoard展示）

   ```bash
   # debian
   sudo apt install -y protobuf-compiler libprotobuf-dev

   # centos
   sudo yum install -y protobuf protobuf-devel protobuf-compiler

   # Python
   pip install protobuf
   ```

4. （可选）安装openssl（RPC TLS认证）& 生成证书密钥

   > [!NOTE]
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

   ```bash
   ssl_certs
   ├── ca.crt (根证书，用于验证其他证书的合法性，必选)
   ├── server.crt (服务器端的证书，用于向客户端证明服务器身份，必选)
   ├── server.key (服务器端的私钥文件，与server.crt配对使用，支持加密，必选)
   └── ca.crl (证书吊销列表，包含已被吊销的证书信息，可选)
   ```

   客户端证书目录结构：

   ```bash
   ssl_certs
   ├── ca.crt (根证书，用于验证其他证书的合法性，必选)
   ├── client.crt (客户端证书，用于向服务器证明客户端身份，必选)
   ├── client.key (客户端的私钥文件，与client.crt配对使用，支持加密，必选)
   └── ca.crl (证书吊销列表，包含已被吊销的证书信息，可选)
   ```

#### 2.3.2 下载源码

下载源码并进入源码目录。

```bash
git clone https://gitcode.com/Ascend/msmonitor.git
cd msmonitor
```

#### 2.3.3 编译并安装dynolog

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
     dpkg -i --force-overwrite dynolog*.deb --ignore-depends
     ```

   - 方式二：使用rpm软件包安装（适用于RedHat/Fedora/openSUSE等系统）。

     ```bash
     rpm -ivh dynolog*.rpm --nodeps
     ```

#### 2.3.4 编译并安装mindstudio_monitor

mindstudio_monitor whl包提供IPCMonitor、MsptiMonitor等公共能力，使用nputrace和npu-monitor功能前必须安装该whl包。

运行时依赖开源三方Python库：

| 依赖 | 用途 |
|------|------|
| pybind11 | Python/C++ 扩展绑定 |
| xlsxwriter | 将采集的性能数据导出为 Excel 文件（`monitor.save("xxx.xlsx")` 功能） |

##### 2.3.4.1 shell脚本一键安装

```bash
chmod +x plugin/build.sh
./plugin/build.sh
```

安装成功打印如下信息：

```bash
Successfully installed mindstudio_monitor-<version> pybind11-<version> xlsxwriter-<version>
```

##### 2.3.4.2 手动安装

1. 安装依赖。

   ```bash
   pip install wheel
   pip install pybind11
   pip install xlsxwriter
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

   ```bash
   Successfully installed mindstudio_monitor-<version> pybind11-<version> xlsxwriter-<version>
   ```

## 3. 卸载

可通过如下步骤卸载：

1. 下载脚本。

   ```bash
   curl -O https://inst.obs.cn-north-4.myhuaweicloud.com/26.0.0/ms_install.py
   ```

   > [!NOTE]
   >
   > - 需要联网环境才能下载，若环境不允许联网或离线，请先在可联网的环境下载该脚本后拷贝到目标设备。
   > - 若执行命令无响应或出现连接失败、SSL证书错误等问题，请参见[FAQ](https://www.hiascend.com/developer/blog/details/02176213671719317003)。

2. 执行卸载。

   ```bash
   python ms_install.py uninstall {tools_name}
   ```

   其中{tools_name}配置为需卸载的工具名称，可通过`python ms_install.py help`命令查询，在打印信息中的Available Tools字段下显示工具名称。

   卸载成功打印如下信息：

   ```bash
   Successfully uninstalled 1 tool ({tools_name})
   ```

## 4. 升级

升级即“先卸后装”。直接执行安装命令，工具将自动卸载旧版本，并引导您完成覆盖安装。

## 5. 日志

用户可以通过配置MSMONITOR_LOG_PATH环境变量，指定到自定义的日志文件路径，默认路径为当前目录下的msmonitor_log。

```bash
export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
```

 /tmp/msmonitor_log为自定义日志文件路径。
