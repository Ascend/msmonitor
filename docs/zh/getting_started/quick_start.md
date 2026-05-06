# msMonitor工具快速入门

下面通过msMonitor常见的使用场景介绍msMonitor工具快速入门：

1. 先使用npu-monitor功能获取关键算子耗时。
2. 当发现监测到关键算子耗时劣化，使用nputrace功能采集详细性能数据做分析。

**前提条件**

完成msMonitor工具安装，具体请参见《[msMonitor工具安装指南](install_guide.md)》。

**操作步骤**

1. 启动dynolog daemon进程。

   命令示例如下：

   ```bash
   # 命令行方式开启dynolog daemon
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs
   
   # 如需使用Tensorboard展示数据，传入参数--metric_log_dir用于指定Tensorboard文件落盘路径
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs --metric_log_dir /tmp/metric_log_dir    # dynolog daemon的日志路径为：/var/log/dynolog.log
   ```

2. 配置msMonitor环境变量。

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. 设置LD_PRELOAD启动MSPTI（启动npu-monitor功能设置）。

   ```bash
   # 默认路径示例：export LD_PRELOAD=/usr/local/Ascend/ascend-toolkit/latest/lib64/libmspti.so
   export LD_PRELOAD=<CANN toolkit安装路径>/ascend-toolkit/latest/lib64/libmspti.so
   ```

4. 启动训练或推理任务。

   以下run*_ai_*task.sh为用户脚本示例，请以实际情况为准。

   ```bash
   bash run_ai_task.sh
   ```

5. 使用dyno命令行触发npu-monitor监测关键算子耗时。

   ```bash
   # 开启npu-monitor，上报周期30s, 上报数据类型为Kernel
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel
   
   # 关闭npu-monitor
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-stop
   ```

6. 使用dyno命令行触发nputrace采集详细trace数据（需要关闭npu-monitor功能才能触发nputrace功能）。

   ```bash
   # 从第10个step开始采集，采集2个step，采集框架、CANN和device数据，同时采集完后自动解析以及解析完成不做数据精简，落盘路径为/tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   ```
