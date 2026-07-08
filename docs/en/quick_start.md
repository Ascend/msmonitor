# msMonitor Quick Start

<!-- md-trans-meta sourceCommit=unknown translatedAt=2026-06-25T02:21:30.827Z pushedAt=2026-06-25T05:44:19.189Z -->

The following introduces the msMonitor quick start through common usage scenarios.

1. First, use the npu-monitor function to obtain the latency of key operators.

2. When critical operator latency degradation is detected, use nputrace to collect detailed profile data for analysis.

**Prerequisites**

Complete the msMonitor installation. For details, see [msMonitor Installation Guide](install_guide.md).

**Procedure**

1. Start the dynolog daemon process.

   Example command:

   ```bash
   # Start dynolog daemon via command line
   dynolog --enable-ipc-monitor --certs-dir /home/server_certs
   
   # To display data using TensorBoard, pass --metric_log_dir to specify the TensorBoard file storage path.
   dynolog --enable-ipc-monitor --certs-dir /home/server_certs --metric_log_dir /tmp/metric_log_dir    # The log path for the dynolog daemon is: /var/log/dynolog.log
   ```

2. Configure the msMonitor environment variables.

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. Set `LD_PRELOAD` to enable MSPTI (configuration for enabling the npu-monitor feature).

   ```bash
   # Default path example: export LD_PRELOAD=/usr/local/Ascend/ascend-toolkit/latest/lib64/libmspti.so
   export LD_PRELOAD=<CANN toolkit installation path>/ascend-toolkit/latest/lib64/libmspti.so
   ```

4. Start the training or inference task.

   The following `run_ai_task.sh` is a user script example. Use the actual script.

   ```bash
   bash run_ai_task.sh
   ```

5. Use the `dyno` command to trigger npu-monitor to monitor the latency of key operators.

   ```bash
   # Enable npu-monitor with a reporting interval of 30s and reporting data type of Kernel
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Kernel
   
   # Disable npu-monitor
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-stop
   ```

6. Use the `dyno` command to trigger nputrace to collect detailed trace data (the npu-monitor function must be disabled before nputrace can be triggered).

   ```bash
   # Starting from step 10, collect data for 2 steps, including the framework, CANN, and device. After collection, enable automatic parsing without data reduction. Save the output to /tmp/profile_data
   dyno --certs-dir /home/client_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   ```
