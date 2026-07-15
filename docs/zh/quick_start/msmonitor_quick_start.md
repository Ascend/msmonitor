# msMonitor工具快速入门

下面通过msMonitor常见的使用场景介绍msMonitor工具快速入门：

1. 先使用npu-monitor功能获取关键算子耗时。
2. 当发现监测到关键算子耗时劣化，使用nputrace功能采集详细性能数据做分析。

**前置条件**

完成msMonitor工具安装，具体请参见《[msMonitor工具安装指南](../install_guide/msmonitor_install_guide.md)》。

**操作步骤**

1. 启动dynolog daemon进程。

   命令示例如下：

   ```bash
   # 命令行方式开启dynolog daemon
   dynolog --certs-dir NO_CERTS --enable-ipc-monitor

   # 如需使用TensorBoard展示数据，传入参数--metric_log_dir用于指定TensorBoard文件落盘路径
   dynolog --certs-dir NO_CERTS --enable-ipc-monitor --metric_log_dir /tmp/metric_log_dir    # dynolog daemon的日志路径为：/var/log/dynolog.log
   ```

   > [!NOTE] Note
   > `--certs-dir NO_CERTS` 表示不使用证书验证，仅用于测试环境，后文同理。在生产环境中，建议使用证书验证，以确保数据传输的安全性。详情请参见 [dynolog_instruct](../user_guide/dynolog_instruct.md)。

2. 配置msMonitor环境变量。

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. 设置LD_PRELOAD环境变量使能msPTI（启动npu-monitor功能设置）。

   ```bash
   # export LD_PRELOAD=<CANN Toolkit安装路径>/cann/lib64/libmspti.so
   # 默认路径示例：
   export LD_PRELOAD=/usr/local/Ascend/cann/lib64/libmspti.so
   ```

4. 启动训练任务。

   以下 `train.py` 是一个完整的 PyTorch 训练脚本示例，使用随机生成的数据，无需外部数据集即可运行。将脚本保存为 `train.py` 并执行：

   ```python
   import torch
   import torch.nn as nn

   # 定义一个简单的全连接网络
   class SimpleModel(nn.Module):
       def __init__(self):
           super(SimpleModel, self).__init__()
           self.net = nn.Sequential(
               nn.Linear(64, 128),
               nn.ReLU(),
               nn.Linear(128, 64),
               nn.ReLU(),
               nn.Linear(64, 10)
           )

       def forward(self, x):
           return self.net(x)

   # 随机生成训练数据，无需外部数据集
   batch_size = 32
   num_steps = 100
   input_size = 64
   num_classes = 10

   device = torch.device("npu:0" if torch.npu.is_available() else "cpu")
   model = SimpleModel().to(device)
   criterion = nn.CrossEntropyLoss()
   optimizer = torch.optim.SGD(model.parameters(), lr=0.01)

   for step in range(num_steps):
       # 使用 torch.randn 随机生成输入和标签
       inputs = torch.randn(batch_size, input_size, device=device)
       labels = torch.randint(0, num_classes, (batch_size,), device=device)

       outputs = model(inputs)
       loss = criterion(outputs, labels)

       optimizer.zero_grad()
       loss.backward()
       optimizer.step()

       if step % 10 == 0:
           print(f"Step {step}, Loss: {loss.item():.4f}")
   ```

   执行训练脚本：

   ```bash
   python train.py
   ```

   > 若使用 PyTorch 原生优化器（如 `torch.optim.SGD`、`torch.optim.Adam`），msMonitor 可自动识别训练迭代边界，无需额外修改代码。

5. 使用dyno命令行触发npu-monitor监测关键算子耗时。

   ```bash
   # 开启npu-monitor，上报周期30s, 上报数据类型为Kernel
   dyno --certs-dir NO_CERTS npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel

   # 关闭npu-monitor
   dyno --certs-dir NO_CERTS npu-monitor --npu-monitor-stop
   ```

   npu-monitor 功能详细介绍和采集结果说明请参见 [npumonitor_instruct](../user_guide/npumonitor_instruct.md)。

6. 使用dyno命令行触发nputrace采集详细trace数据（需要关闭npu-monitor功能才能触发nputrace功能）。

   ```bash
   # 从第10个step开始采集，采集2个step，采集框架、CANN和device数据，同时采集完后自动解析以及解析完成不做数据精简，落盘路径为/tmp/profile_data
   dyno --certs-dir NO_CERTS nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   ```

   nputrace 功能详细介绍和采集结果说明请参见 [nputrace_instruct](../user_guide/nputrace_instruct.md)。
