# msMonitor 安装指南

<br>

## 1. 安装说明

msMonitor工具仅支持在Linux系统下使用，兼容aarch64和x86 CPU架构，支持[在线安装](#21-在线安装)、[离线安装](#22-离线安装)、[源码安装](#23-源码安装)三种安装方式，请根据您的实际环境选择最合适的方案。

## 2. 安装方式

### 2.1 在线安装

若您的设备具备互联网访问能力，可通过一条命令自动完成工具的下载与安装。请参见昇腾社区MindStudio[下载](https://www.hiascend.com/developer/software/mindstudio/download)页面，选择对应的CANN版本，并在安装方式中选择“在线安装”，系统将引导您完成后续操作。

### 2.2 离线安装

对处于企业内网等无外网环境的设备，请先在可联网的机器上下载完整的离线安装包，再将其传输至目标设备进行安装。请参见昇腾社区MindStudio[下载](https://www.hiascend.com/developer/software/mindstudio/download)页面，选择对应的CANN版本，并在安装方式中选择“离线安装”，获取对应的安装包及操作指引。

### 2.3 源码安装

如需使用最新代码的功能，或对源码进行修改以增强功能，可下载本仓库代码，自行编译、打包工具并完成安装。

#### 2.3.1 环境准备

源码编译统一使用 MindStudio 标准构建环境。

- 日常开发或使用已发布镜像，请参考《[MindStudio工具开发环境安装指导](https://gitcode.com/Ascend/msot/blob/master/docs/zh/common/dev_env_setup.md)》。
- 需要从基础操作系统复现环境、执行源码构建验证或单元测试验证时，必须参考《[MindStudio统一构建镜像制作指南](https://gitcode.com/Ascend/msot/blob/master/docs/zh/common/docker_image_build_guide.md)》，从openEuler基础镜像现场构建环境镜像。

本文档后续的源码编译和单元测试命令，均在上述指定镜像容器或现场构建的环境镜像的容器中执行，CANN 软件包版本、GCC 版本和 Python 版本以统一镜像制作指南为准，本仓库不重复维护。

镜像构建完成后，必须使用统一镜像制作指南第 7 章给出的 `ctr_in.py` 命令，在交互式终端中启动并进入容器。不得使用普通 `docker run` 创建容器，也不得使用 `docker exec <容器名> bash -c '<命令>'` 替代交互式环境；否则可能跳过 Python、GCC 和 CANN 环境初始化。

进入 `ctr_in.py` 打开的交互式容器 Shell 后，执行如下命令克隆本仓库：

```bash
cd ~
git clone https://gitcode.com/Ascend/msmonitor.git
```

> 可选：如需安装openssl（RPC TLS认证）& 生成证书密钥，请参考 [第5节](#5-安装opensslrpc-tls认证生成证书密钥)。

#### 2.3.2 编译并安装 dynolog

保持在 `ctr_in.py` 打开的同一个交互式容器 Shell 中，在仓库根目录执行以下命令，自动完成依赖下载与构建：

```bash
cd ~/msmonitor
python3 build.py -e dynolog=true
```

构建成功后，安装包将生成在 `artifacts/` 目录下。

安装dynolog，根据系统选择对应方式：

```bash
cd ~/msmonitor/artifacts
# Debian/Ubuntu 等系统
dpkg -i --force-overwrite dynolog*.deb --ignore-depends
# RedHat/Fedora/openSUSE 等系统
rpm -ivh dynolog*.rpm --nodeps
```

验证 dynolog 安装是否成功：

```bash
dyno --help
dynolog --help
```

若输出不报错，且能显示帮助信息，则表明安装成功。

#### 2.3.3 编译并安装 mindstudio_monitor

mindstudio_monitor whl包提供IPCMonitor、MsptiMonitor等公共能力，使用nputrace和npu-monitor功能前必须安装该whl包。

保持在 `ctr_in.py` 打开的同一个交互式容器 Shell 中，在仓库根目录执行以下命令，自动完成依赖下载与构建：

```bash
cd ~/msmonitor
python3 build.py -e whl=true
```

构建成功后，安装包将生成在 `artifacts/` 目录下。

安装方法：

```bash
cd ~/msmonitor/artifacts
pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl
```

验证安装是否成功：

```bash
python3 -c "import msmonitor; print('All is OK')"
```

若输出不报错，且能显示'All is OK'，则表明安装成功。

## 3. 卸载

可通过如下步骤卸载：

1. 下载脚本。

   ```bash
   curl -O https://inst.obs.cn-north-4.myhuaweicloud.com/26.1.0/ms_install.py
   ```

   > [!NOTE]
   >
   > - 需要联网环境才能下载，若环境不允许联网或处于离线状态，请先在可联网的环境下载该脚本后拷贝到目标设备。
   > - 若执行命令无响应或出现连接失败、SSL证书错误等问题，请参见[FAQ](https://www.hiascend.com/developer/blog/details/02176213671719317003)。

2. 执行卸载。

   ```bash
   python ms_install.py uninstall {tools_name}
   ```

   其中{tools_name}配置为需卸载的工具名称，可通过`python ms_install.py help`命令查询，在打印信息中的Available Tools字段下显示工具名称。

   卸载成功打印如下信息：

   ```text
   Successfully uninstalled 1 tool ({tools_name})
   ```

## 4. 升级

升级即“先卸后装”。直接执行安装命令，工具将自动卸载旧版本，并引导您完成覆盖安装。

可通过`dyno --version`命令查看当前环境的版本信息，再选择需要升级的版本。升级版本时需要关注版本配套关系，请参见《[版本说明](https://gitcode.com/Ascend/release-management/blob/master/MindStudio/26.1.0/release_notes.md)》。

## 5. 安装openssl（RPC TLS认证）生成证书密钥

dyno CLI与dynolog daemon之间的RPC通信使用TLS证书密钥加密，在启动dyno和dynolog二进制时可以指定证书密钥存放的路径，路径下需要满足如下结构和名称。

用户应使用与自己需求相符的密钥生成和存储机制，并保证密钥安全性与机密性。当前仅支持RSA-SHA256和RSA-SHA512两种证书签名算法。

服务端证书目录结构：

```text
ssl_certs
├── ca.crt (根证书，用于验证其他证书的合法性，必选)
├── server.crt (服务器端的证书，用于向客户端证明服务器身份，必选)
├── server.key (服务器端的私钥文件，与server.crt配对使用，支持加密，必选)
└── ca.crl (证书吊销列表，包含已被吊销的证书信息，可选)
```

客户端证书目录结构：

```text
ssl_certs
├── ca.crt (根证书，用于验证其他证书的合法性，必选)
├── client.crt (客户端证书，用于向服务器证明客户端身份，必选)
├── client.key (客户端的私钥文件，与client.crt配对使用，支持加密，必选)
└── ca.crl (证书吊销列表，包含已被吊销的证书信息，可选)
```
