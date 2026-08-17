# msMonitor Quick Start

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T07:48:10.212Z pushedAt=2026-08-12T07:49:07.091Z -->

The following introduces the msMonitor quick start through common usage scenarios.

1. Use the npu-monitor function to obtain the latency of key operators.

2. When critical operator latency degradation is detected, use nputrace to collect detailed performance data for analysis.

**Prerequisites**

Complete the msMonitor tool installation. For details, see *[msMonitor Installation Guide](../install_guide/msmonitor_install_guide.md)*.

**Procedure**

1. Start the dynolog daemon process.

   Example command:

   ```bash
   # Start the dynolog daemon from the command line
   dynolog --certs-dir NO_CERTS --enable-ipc-monitor

   # If you need to use TensorBoard to display data, pass the --metric_log_dir parameter to specify the TensorBoard file write path.
   dynolog --certs-dir NO_CERTS --enable-ipc-monitor --metric_log_dir /tmp/metric_log_dir    # The log path of the dynolog daemon is: /var/log/dynolog.logar/log/dynolog.log
   ```

   > [!NOTE]
   > 
   > `--certs-dir NO_CERTS` indicates that certificate verification is not used. This is intended for test environments only, and the same applies below. In production environments, certificate verification is recommended to ensure data transmission security. For details, see [dynolog_instruct](../user_guide/dynolog_instruct.md).

2. Configure the msMonitor environment variables.

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. Set the `LD_PRELOAD` environment variable to enable msPTI (configuration for enabling the npu-monitor feature).

   ```bash
   # export LD_PRELOAD=<CANN Toolkit installation path>/cann/lib64/libmspti.so
   # Default path example
   export LD_PRELOAD=/usr/local/Ascend/cann/lib64/libmspti.so
   ```

4. Start the training task.

   The following `train.py` is a complete PyTorch training script example that uses randomly generated data and can run without an external dataset. Save the script as `train.py` and execute it:

   ```python
   import torch
   import torch.nn as nn

   # Define a simple fully connected network
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

   # Randomly generate training data without an external dataset
   batch_size = 32
   num_steps = 100
   input_size = 64
   num_classes = 10

   device = torch.device("npu:0" if torch.npu.is_available() else "cpu")
   model = SimpleModel().to(device)
   criterion = nn.CrossEntropyLoss()
   optimizer = torch.optim.SGD(model.parameters(), lr=0.01)

   for step in range(num_steps):
       # Use torch.randn to randomly generate inputs and labels
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

   Execute the training script:

   ```bash
   python train.py
   ```

   > If you use PyTorch native optimizers (such as `torch.optim.SGD` or `torch.optim.Adam`), msMonitor can automatically identify training iteration boundaries without requiring additional code modifications.

5. Use the `dyno` command to trigger npu-monitor to monitor the latency of key operators.

   ```bash
   # Start npu-monitor with a reporting interval of 30s and the reporting data type set to Kernel
   dyno --certs-dir NO_CERTS npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel

   # Stop npu-monitor
   dyno --certs-dir NO_CERTS npu-monitor --npu-monitor-stop
   ```

   For detailed description of the npu-monitor feature and collection results, see [npumonitor_instruct](../user_guide/npumonitor_instruct.md).

6. Use the `dyno` command to trigger nputrace to collect detailed trace data (the npu-monitor function must be disabled before nputrace can be triggered).

   ```bash
   # Start collection from step 10, collect  framework, CANN, and device data 2 steps, automatically parse after collection, and do not perform data simplification after parsing. The output path is /tmp/profile_data.
   dyno --certs-dir NO_CERTS nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   ```

   For detailed description of the nputrace feature and collection results, see [nputrace_instruct](../user_guide/nputrace_instruct.md).
