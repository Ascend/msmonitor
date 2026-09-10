# npu-monitor Usage Guide

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T08:31:16.987Z pushedAt=2026-08-12T08:32:52.181Z -->

## Introduction

The npu-monitor tool is a lightweight daemon that monitors the execution time of key operators.

## Preparation

Install the msMonitor tool. For details, see *[msMonitor Installation Guide](../install_guide/msmonitor_install_guide.md)*. It is recommended to install using the downloaded software package.

Start npu-monitor through the npu-monitor subcommand in the dyno CLI:

```bash
dyno --certs-dir <CERT_DIR> npu-monitor [options]
```

**Constraints**

- The value passed to `--certs-dir` in dyno and dynolog must be consistent.

- `CERT_DIR` can be used to pass a certificate path. If TLS certificate keys are not used, set it to `NO_CERTS`.

## npu-monitor Feature Description

**Feature Description**

Enables npu-monitor performance monitoring.

**Syntax**

```bash
dyno npu-monitor [options] --help
```

The options of npu-monitor are as follows.

**Option Description**

| Subcommand                   |    Mandatory/Optional    | Description                                                                                                                                                  | PyTorch Support | MindSpore Support |
|-----------------------|:-----------:|------------------------------------------------------------------------------------------------------------------------------------------------------|:---------:|:-----------:|
| --npu-monitor-start   | Optional | (Action) Enables performance monitoring. It takes effect after being set and is disabled by default.                                                                                                           | Y | Y |
| --npu-monitor-stop    | Optional | (Action) Stops performance monitoring. It takes effect after being set and is disabled by default.                                                                                                             | Y | Y |
| --report-interval-s   | Optional | (u32) Performance monitoring data reporting interval, in seconds. Must be set at startup. Default value: `60`.                                                                                               | Y | Y |
| --duration            | Optional | (f32) Performance monitoring data collection duration, in seconds. Must be set at startup. Default value: `0.0`, meaning no limit on collection duration. Can only be set when npu-monitor starts and cannot be modified during runtime.                                                                         | Y | Y |
| --mspti-activity-kind | Optional | (String) Performance monitoring data reporting data type. Single or multiple types can be set, separated by commas. The global reporting type is refreshed each time it is set. Optional value range: [`Marker`, `Kernel`, `API`, `Hccl`, `Memory`, `MemSet`, `MemCpy`, `Communication`, `AclAPI`, `NodeAPI`, `RuntimeAPI`]. Default value: `Marker`. | Y | Y |
| --log-file            | Optional | (String) Path for persisting collected performance data to disk. Currently supports exporting only when `mspti-activity-kind` is set to `Marker`, `Kernel`, `API`, `Communication`, `AclAPI`, `NodeAPI`, or `RuntimeAPI`. The persistent storage data format can be DB or Jsonl (for details, see the `export-type` parameter description). Default value: empty, meaning no data is persisted. | Y | Y |
| --export-type         | Optional | (String) Format for persisting collected performance data to disk. Takes effect only after the user sets the `log-file` parameter. Optional value range: [`DB`, `Jsonl`]. Default value: `DB`. Can only be set when npu-monitor starts and cannot be modified during runtime.<br> **1.** If set to `DB`, the persisted data is in DB format, and the file name is `msmonitor_{process_id}_{timestamp}_{rank_id}.db`. For details about DB content, see [DB Format Performance Data File Reference](https://gitcode.com/Ascend/msprof/blob/master/docs/en/user_guide/profile_data_file_references_db.md). You can use the [MindStudio Insight](https://gitcode.com/Ascend/msinsight/blob/master/docs/en/user_guide/overview.md) tool for visual presentation (MindStudio Insight does not currently support presenting `msmonitor.db` data collected in single-process multi-card scenarios). <br> **2.** If set to `Jsonl`, the persisted data is in Jsonl format, and the file name is `msmonitor_{process_id}_{timestamp}_{rank_id}.jsonl`. Each line of the Jsonl file contains a complete piece of performance data in JSON format. The following environment variables can be set to adjust the persistence process: <br> **MSMONITOR_JSONL_BUFFER_CAPACITY**: Sets the RingBuffer size for persistence. This parameter must be a power of 2 ($2^{n}$). Default value: 524288 ($2^{19}$). Supported range: [8192, 2097152] (i.e., [$2^{13}$, $2^{21}$]). <br> **MSMONITOR_JSONL_MAX_DUMP_INTERVAL**: Sets the maximum interval for persistence (unit: ms). When the interval between the current time and the last persistence exceeds this threshold, persistence is automatically triggered. Default value: 30000 ms. Minimum limit: 1000 ms. <br> **MSMONITOR_JSONL_ROTATE_LOG_LINES**: Sets the upper limit of JSON data entries in a single Jsonl file. Exceeding this threshold will create a new file for persistence. Default value: 10000. Supported range: [100, 500000]. <br> **MSMONITOR_JSONL_ROTATE_LOG_FILES**: Sets the number of Jsonl files persisted in a single collection. Exceeding this threshold will delete the earliest persisted file. Default value: `-1` (this feature is disabled). When manually set, the minimum limit is 2. | Y | Y |
| --json-rotate-log-lines | Optional | (String) Upper limit of JSON data entries in a single Jsonl file during Jsonl persistence. Takes effect only when `--log-file` and `--export-type Jsonl` are configured.<br>It is not passed by default and takes precedence over the environment variable `MSMONITOR_JSONL_ROTATE_LOG_LINES`. If neither the command line nor the environment variable is set, the default value `10000` is used. When manually set, a positive integer in the range [100, 500000] must be passed. | Y | Y |
| --json-rotate-log-files | Optional | (String) Number of Jsonl files retained in a single collection during Jsonl persistence. When this threshold is exceeded, the earliest persisted file is deleted. Takes effect only when `--log-file` and `--export-type Jsonl` are configured.<br>It is not passed by default and takes precedence over the environment variable `MSMONITOR_JSONL_ROTATE_LOG_FILES`. If neither the command line nor the environment variable is set, the default value `-1` is used, meaning file count cleanup is disabled. When manually set, `-1` or a positive integer greater than or equal to 2 must be passed. | Y | Y |
| --filter              | Optional | (String) Filters performance data by the data names to be collected. Different data types are separated by semicolons, different data names are separated by commas, and data types and names are separated by colons.<br>Fuzzy matching is supported. You do not need to configure the full name; only keywords are required. When the configured value contains semicolons, the entire value must be enclosed in double quotation marks.<br>Configuration example: `--filter "<activity_kind>:<data>[,<data>][;<activity_kind>:<data>[,<data>]]"`. Optional value range for activity_kind: [`Marker`, `Kernel`, `API`, `Communication`, `AclAPI`, `NodeAPI`, `RuntimeAPI`]. By default, no filtering is applied and all data is retained. | Y | Y |

**Usage Example**

1. Start the dynolog daemon process. For details, see [dynolog](./dynolog_instruct.md).

   ```bash
   # Start the dynolog daemon via command line
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs

   # To use TensorBoard for data visualization, pass --metric_log_dir to specify the persistent storage path for TensorBoard files.
   # Example:
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs --metric_log_dir /tmp/metric_log_dir
   ```

2. Configure the dynolog environment variables.

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. (Optional) Configure the msMonitor log path. The default path is msmonitor_log in the current directory.

   ```bash
   export MSMONITOR_LOG_PATH=<LOG PATH>
   # Example:
   export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
   ```

4. Set `LD_PRELOAD` to enable msPTI.

   ```bash
   export LD_PRELOAD=<CANN Toolkit installation path>/cann/lib64/libmspti.so
   # Default path example:
   export LD_PRELOAD=/usr/local/Ascend/cann/lib64/libmspti.so
   ```

5. Start the training or inference task.

   ```bash
   # The training task requires using the PyTorch optimizer or inheriting the native optimizer
   bash train.sh
   ```

6. Use the dyno CLI to start npu-monitor.

   ```bash
   # Example 1: Start performance monitoring with default configuration
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start

   # Example 2: Pause performance monitoring
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-stop

   # Example 3: Dynamically modify collection parameters during performance monitoring
   # Set the reporting period to 30s and the reported data type to Marker and Kernel, and retain only data of the Kernel type whose operator name contains the keyword "Mul"
   dyno --certs-dir /home/ssl_certs npu-monitor --report-interval-s 30 --mspti-activity-kind Marker,Kernel --filter Kernel:Mul

   # Example 4: Configure collection parameters when starting performance monitoring
   # Set the reporting period to 25s and the reported data type to Marker and Kernel, and retain only data of the Kernel type whose operator name contains the keyword "Relu".
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 25 --mspti-activity-kind Marker,Kernel --filter Kernel:Relu

   # Example 5: Configure collection parameters when Performance Monitoring is enabled, enable data collection to persistent storage, with the storage format being the default DB format
   # Data persistent storage path is /tmp/msmonitor_db, storage period is 30s, and collection data types are Marker, Kernel, and Communication.
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel,Communication --log-file /tmp/msmonitor_db

   # Example 6: Write to persistent storage in Jsonl format, and set Jsonl file rotation configuration via command-line parameters
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel,Communication --log-file /tmp/msmonitor_jsonl --export-type Jsonl --json-rotate-log-lines 10000 --json-rotate-log-files 5

   # Example 7: Modify configuration when Performance Monitoring is enabled in a multi-machine scenario
   # In a multi-machine scenario, send parameter information to a specific machine x.x.x.x. The parameters indicate a reporting period of 30s and reporting data types of Marker and Kernel.
   dyno --certs-dir /home/ssl_certs --hostname x.x.x.x npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel
   ```

## Output File Description

View the data reported by TensorBoard.

```bash
# Ensure that TensorBoard is installed
pip install tensorboard

# Run TensorBoard
tensorboard --logdir=<metric_log_dir> # metric_log_dir is the path specified by the --metric_log_dir parameter in the example dynolog command line

# Access http://localhost:6006 from a browser to view the corresponding visualization charts, where localhost is the server IP address and 6006 is the default TensorBoard port.
```

For detailed TensorBoard usage parameters, see <https://github.com/tensorflow/tensorboard>.
