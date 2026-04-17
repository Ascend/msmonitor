# npu-monitor

## Overview

The npu-monitor tool is a lightweight, resident background tool that monitors the time consumption of key operators.

## Preparations

The npu-monitor tool is enabled by running the npu-monitor subcommand in dyno CLI.

```bash
dyno --certs-dir <CERT_DIR> npu-monitor [SUBCOMMANDS]
```

**Constraints**

- The value of `--certs-dir` must be the same in dyno and dynolog.
- `CERT_DIR` can be set to the certificate path. If the TLS certificate key is not used, set `CERT_DIR` to `NO_CERTS`.

## npu-monitor Functions

**Function**

Enables npu-monitor performance monitoring.

**Syntax**

```bash
dyno npu-monitor [SUBCOMMANDS] --help
```

The following table lists the subcommands of npu-monitor.

**Subcommand Description**

| Subcommand                  | Type| Description                                                                                                                                                 | Supported by PyTorch (Y/N)| Supported by MindSpore (Y/N)|    Mandatory (Y/N)    |
|-----------------------|-------|------------------------------------------------------------------------------------------------------------------------------------------------------|:---------:|:-----------:|:-----------:|
| --npu-monitor-start   | action | Enables performance monitoring. This feature is disabled by default and only takes effect after configuration.                                                                                                                             | Y | Y | N |
| --npu-monitor-stop    | action | Stops performance monitoring. This feature is disabled by default and only takes effect after configuration.                                                                                                                              | Y | Y | N |
| --report-interval-s   | u32 | Indicates the interval for reporting profile data, in seconds. This subcommand needs to be set during startup. The default value is `60`.                                                                                                                     | Y | Y | N |
| --duration            | f32 | Indicates the duration for collecting profile data, in seconds. This subcommand needs to be set during startup. The default value is `0.0`, indicating that the collection duration is not limited. This subcommand can be set only when npu_monitor is started and cannot be modified during running.                                                                                       | Y | Y | N |
| --mspti-activity-kind | String | Indicates the type of profile data to be reported. You can set one or more types. Multiple types must be separated by commas (,). The type is updated globally each time you set this subcommand. The value range is [`Marker`, `Kernel`, `API`, `Hccl`, `Memory`, `MemSet`, `MemCpy`, `Communication`, `AclAPI`, `NodeAPI`, 'RuntimeAPI']. The default value is `Marker`.| Y | Y | N |
| --log-file            | String | Indicates the path for outputting profile data to drives. Currently, only the data of the following types can be exported: `Marker`, `Kernel`, `API`, `Communication`, `AclAPI`, `NodeAPI`, and `RuntimeAPI`. The output data format can be DB or JSONL (for details, see the description of `export-type`). The default value is empty, indicating that data is not output to drives.| Y | Y | N |
| --export-type         | String | Indicates the format of the profile data to be output to drives. This parameter is valid only when `log-file` is set. The value range is [`DB`, `Jsonl`]. The default value is `DB`. This subcommand can be set only when npu_monitor is started and cannot be modified during running.<br> **1.** `DB`: The output data is in DB format and the file name is `msmonitor_{process_id}_{timestamp}_{rank_id}.db`. For details about the content, see [description of data exported from msprof in DB Format](https://www.hiascend.com/document/detail/zh/canncommercial/83RC1/devaids/Profiling/atlasprofiling_16_1144.html). [MindStudio Insight](https://www.hiascend.com/document/detail/zh/mindstudio/82RC1/GUI_baseddevelopmenttool/msascendinsightug/Insight_userguide_0002.html) can be used to visualize the data. (This tool does not support the display of **msmonitor.db** data collected in the single-process multi-device scenario.)<br> **2.** `Jsonl`: The output data is in JSONL format. The file name is `msmonitor_{process_id}_{timestamp}_{rank_id}.jsonl`. Each line in the JSONL file contains a complete profile data record in JSON format. You can set the following environment variables to adjust the output process:<br> `MSMONITOR_JSONL_BUFFER_CAPACITY`: size of the ring buffer for outputting data to drives. The value must be a power of 2 ($2^{n}$). The default value is 524288 ($2^{19}$). The value range is [8192, 2097152] ($2^{13}$ to $2^{21}$).<br> `MSMONITOR_JSONL_MAX_DUMP_INTERVAL`: maximum interval for outputting data to drives, in milliseconds. If the interval between the current time and the last time data is output to drives exceeds the threshold, data output is automatically triggered. The default value is `30000` ms, and the minimum value is `1000` ms.<br> `MSMONITOR_JSONL_ROTATE_LOG_LINES`: maximum number of JSON data records in a single JSONL file. If the number is exceeded, a new file is created and output to drives. The value ranges from [100, 500000]. The default value is `10000`.<br> `MSMONITOR_JSONL_ROTATE_LOG_FILES`: maximum number of JSONL files that can be output to drives at a time. If the number is exceeded, the earliest files are deleted. The default value is `-1` (this function is disabled). The minimum value is `2`.| Y | Y | N |
| --filter              | String | Filters profile data by the name of the data to be collected. Different data types are separated by semicolons (;), different data names are separated by commas (,), and the data type and name are separated by colons (:). Fuzzy match is supported. You only need to set the keywords. If the value contains semicolons (;), enclose the entire value with double quotation marks (""). Example: `--filter "<activity_kind>:<data>[,<data>][;<activity_kind>:<data>[,<data>]]"`. The options of **activity_kind** are as follows: `Marker`, `Kernel`, `API`, `Communication`, `AclAPI`, `NodeAPI`, and `RuntimeAPI`. By default, all data is retained.| Y | Y | N |

1. Start the dynolog daemon process. For details, see [dynolog](./dynolog_instruct.md).

   ```bash
   # Enable the dynolog daemon in CLI mode.
   dynolog --enable-ipc-monitor --certs-dir /home/server_certs

   # To use TensorBoard to display data, pass --metric_log_dir to specify the output path of the TensorBoard file.
   # Example:
   dynolog --enable-ipc-monitor --certs-dir /home/server_certs --metric_log_dir /tmp/metric_log_dir
   ```

2. Configure the dynolog environment variable.

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. (Optional) Configure the msMonitor log path. The default path is `msmonitor_log` in the current directory.

   ```bash
   export MSMONITOR_LOG_PATH=<LOG PATH>
   # Example:
   export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
   ```

4. Set `LD_PRELOAD` to enable MSPTI.

   ```bash
   export LD_PRELOAD=<CANN_Toolkit_installation_path>/cann/lib64/libmspti.so
   # Example:
   export LD_PRELOAD=/usr/local/Ascend/cann/lib64/libmspti.so
   ```

5. Start a training or inference job.

   ```bash
   # The PyTorch optimizer or native optimizer is required in the training job.
   bash train.sh
   ```

6. Use the dyno CLI to start npu-monitor.

   ```bash
   # Example 1: Enable performance monitoring and use the default configuration.
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-start

   # Example 2: Pause performance monitoring.
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-stop

   # Example 3: Modify the configuration during performance monitoring.
   # Set the reporting period to 30s and the reported data type to Marker and Kernel, and retain only data of the Kernel type whose operator name contains the keyword "Mul".
   dyno --certs-dir /home/client_certs npu-monitor --report-interval-s 30 --mspti-activity-kind Marker,Kernel --filter Kernel:Mul

   # Example 4: Modify the configuration when performance monitoring is enabled.
   # Set the reporting period to 30s and the reported data type to Marker and Kernel, and retain only data of the Kernel type whose operator name contains the keyword "Mul".
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel --filter Kernel:Mul

   # Example 5: Modify the configuration when performance monitoring is enabled and enable data output.
   # The data output path is /tmp/msmonitor_db, the output period is 30s, and the collected data types are Marker, Kernel, and Communication.
   dyno --certs-dir /home/client_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel,Communication --log-file /tmp/msmonitor_db

   # Example 6: Modify the configuration when performance monitoring is enabled in the multi-server scenario.
   # In the multi-server scenario, send parameter information to a specific server x.x.x.x. The parameters indicate that the reporting period is 30s and the reported data type is Marker and Kernel.
   dyno --certs-dir /home/client_certs --hostname x.x.x.x npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel
   ```

## Output File Description

Observe the data reported by TensorBoard.

```bash
# Ensure that TensorBoard is installed.
pip install tensorboard

# Run TensorBoard.
tensorboard --logdir=<metric_log_dir> # metric_log_dir is the path specified by --metric_log_dir in the dynolog command line.

# Access http://localhost:6006 from the browser to view the corresponding visualization chart. localhost indicates the IP address of the server, and 6006 indicates the default port of TensorBoard.
```

For details about TensorBoard, visit <https://github.com/tensorflow/tensorboard>.
