# 版本说明

msMonitor由三个文件组成，如下表所示。

其中dyno和dynolog可以被打包为deb包或者rpm包。目前msMonitor支持在[PyTorch](https://gitcode.com/Ascend/pytorch)框架和[MindSpore](https://www.mindspore.cn/)框架上运行。

> [!NOTE] Note
> MindStudio 26.0.0 及之后的版本，请在[msMonitor Releases](https://gitcode.com/Ascend/msmonitor/releases)页面下载软件包，之前的软件包请参见《[版本配套说明](#版本配套说明)》。

| 文件名                                                       | 用途                                                         |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| dyno                                                         | dyno客户端二进制文件                                         |
| dynolog                                                      | dynolog服务端二进制文件                                      |
| mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{system_architecture}.whl | msPTI Monitor、IPC等公共能力工具包，{mindstudio_version}表示mindstudio版本号，{python_version}表示python版本号，{system_architecture}表示CPU架构系统 |

**OS兼容性说明**

- Linux系统，支持aarch64和x86 CPU架构。

**版本配套说明**<a name="版本配套说明"></a>

msMonitor详细版本配套关系及对应软件包下载链接如下：

| msMonitor版本  | 发布日期   | 配套CANN版本  | 配套torch_npu版本 | 配套MindSpore版本 | 下载链接                                                     | 校验码                                                       |
| -------------- | ---------- | ------------- | ----------------- | ----------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 8.3.0(aarch64) | 2025-12-29 | 8.3.RC1及以上 | v7.3.0及以上      | 2.7.2及以上       | [aarch64_8.3.0.zip](https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.3.0/aarch64_8.3.0.zip) | 2c675ae346dfc1c70f5e9c7103d6f8c7e53be00dca28ed5f9cc577ac59e4bc44 |
| 8.3.0(x86)     | 2025-12-29 | 8.3.RC1及以上 | v7.3.0及以上      | 2.7.2及以上       | [x86_8.3.0.zip](https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.3.0/x86_8.3.0.zip) | 1a38cc141e67c50eb09ebdc757c1fd3ed54439f227459e71292b2d18bb78e7f0 |
| 8.1.0(aarch64) | 2025-07-11 | 8.1.RC1及以上 | v7.1.0及以上      | 2.7.0-rc1及以上   | [aarch64_8.1.0.zip](https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.1.0/aarch64_8.1.0.zip) | ce136120c0288291cc0a7803b1efc8c8416c6105e9d54c17ccf2e2510869fada |
| 8.1.0(x86)     | 2025-07-11 | 8.1.RC1及以上 | v7.1.0及以上      | 2.7.0-rc1及以上   | [x86_8.1.0.zip](https://ptdbg.obs.cn-north-4.myhuaweicloud.com/profiler/msmonitor/8.1.0/x86_8.1.0.zip) | 097d11c7994793b6389b19259269ceb3b6b7ac5ed77da3949b3f09da2103b7f2 |
