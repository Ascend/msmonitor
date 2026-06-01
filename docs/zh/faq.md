# FAQ

- **Q：dyno CLI发送npu-monitor命令后没有数据上报？**
- A：npu-monitor 功能基于 msPTI 接口开发，如果没有数据上报，如果是 CANN 9.0.0 之前的版本，请先检查 LD_PRELOAD 是否已经正常设置了 libmspti.so 的路径，然后检查 dynolog 日志中是否正常接收到 dyno CLI 的RPC请求。

- **Q：dyno CLI发送采集命令后，无法采集到性能数据？**
- A：通常有以下几种情况，导致 msMonitor 采集数据失败，建议用户逐一排查：
  1. dynolog daemon没有正常启动

     解决方法：请检查 dynolog daemon 是否已经正常启动，当前用户 home 目录下是否生成 dynolog.sock 文件，没有请先启动 dynolog daemon 进程

  2. 没有设置 MSMONITOR_USE_DAEMON 环境变量

     解决方法：请在启动训练或推理服务前，设置 MSMONITOR_USE_DAEMON 环境变量为1，开启 msMonitor 功能。

  3. 遗留的共享内存文件没有删除

     解决方法：msMonitor 底层依赖昇腾 [PyTorch 动态 Profiling](https://gitcode.com/Ascend/pytorch/blob/v2.7.1/docs/zh/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md#%E9%87%87%E9%9B%86%E5%B9%B6%E8%A7%A3%E6%9E%90%E6%80%A7%E8%83%BD%E6%95%B0%E6%8D%AE%EEF%BC%88dynamic_profile%EF%BC%89) 机制实现能力。在 Python 3.8 及以上环境中，工具会在 `/dev/shm` 目录生成命名格式为 `DynamicProfileNpuShm+时间戳` 的二进制共享内存文件。
     进程正常退出时，该文件会自动清理；若通过 pkill 强制终止进程，属于异常退出，相关资源无法自动释放。残留文件会导致短时间(<1h)内再次使用 msMonitor 时，出现数据采集异常。建议在使用 msMonitor 时，启动模型进程前，先检查 `/dev/shm` 目录，若存在历史残留的共享内存文件，请手动删除。

----------------------------------------------------------------------------------------------------------------------------------------------------
