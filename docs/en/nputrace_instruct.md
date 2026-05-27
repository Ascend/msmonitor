# nputrace

## Overview

The nputrace tool is used to obtain detailed profile data of the framework, CANN, and devices.

## Preparations

Install msMonitor. For details, see [msMonitor Installation Guide](./install_guide.md). You are advised to download the software package for installation.

## nputrace Functions

**Function**

Collects profile data.

**Precautions**

As a subcommand of the `dyno` command, `nputrace` requires `--certs-dir`. The value of `--certs-dir` must be the same as that of `--certs-dir` in [dyno](dyno_instruct.md) and [dynolog](dynolog_instruct.md).

**Syntax**

```bash
dyno --certs-dir <CERT_DIR> nputrace [options]
```

`CERT_DIR` indicates the certificate path. If the TLS certificate key is not used, set `CERT_DIR` to `NO_CERTS`. `[options]` is described as follows.

**Option Description**

| Option                  |  Required/Optional | Description                                                                                                                                                                                                                                                                                | Supported by PyTorch (Y/N)| Supported by MindSpore (Y/N)|
|-----------------------|:----------:|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:---------:|:-----------:|
| --job-id              | Optional| ID of a collection task. The value is of the u64 type. The default value is `0`. Native dynolog option.                                                                                                                                                                                                                                             |     N     |      N      |
| --pids                | Optional| PID list of a collection task. The value is of the string type. Multiple PIDs must be separated by commas (,). The default value is `0`. Native dynolog option.                                                                                                                                                                                                                             |     N     |      N      |
| --process-limit       | Optional| Maximum number of collection processes. The value is of the u64 type. The default value is `3`. Native dynolog option.                                                                                                                                                                                                                                                 |     N     |      N      |
| --profile-start-time  | Optional| Unix timestamp for synchronous collection. The value is of the u64 type, in milliseconds. The default value is `0`. Native dynolog option.                                                                                                                                                                                                                                       |     N     |      N      |
| --duration-ms         | Optional| Collection period. The value is of the u64 type. The default value is `500`, in milliseconds. Native dynolog option.                                                                                                                                                                                                                                            |     N     |      N      |
| --iterations          | Mandatory| Total number of steps for collection. The value is of the i64 type. The value must be a positive integer. Native dynolog option. Must be used together with the `--start-step` option.                                                                                                                                                                                                             |     Y     |      Y      |
| --log-file            | Mandatory| Path for outputting collected data. The value is of the string type.                                                                                                                                                                                                                                                              |     Y     |      Y      |
| --start-step          | Mandatory| Start step for collection. The value is of the i64 type. The value must be a positive integer or `-1`. If the value is set to `-1`, the collection starts from the next step.                                                                                                                                                                                                                          |     Y     |      Y      |
| --record-shapes       | Optional| InputShapes and InputTypes collection switch of an operator. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                           |     Y     |      Y      |
| --profile-memory      | Optional| Operator memory information collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                                |     Y     |      Y      |
| --with-stack          | Optional| Python call stack collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                             |     Y     |      Y      |
| --with-flops          | Optional| Operator flops collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                               |     Y     |      N      |
| --with-modules        | Optional| Python call stack collection switch at the modules level. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                   |     Y     |      N      |
| --analyse             | Optional| Automatic analysis switch after collection. The value is of the action type. If this parameter is set, automatic analysis is enabled. If this parameter is not set, automatic analysis is disabled.                                                                                                                                                                                                         |     Y     |      Y      |
| --async-mode          | Optional| Asynchronous analysis switch. The value is of the action type. If this parameter is set, asynchronous analysis is enabled. If this parameter is not set, synchronous analysis is used. This option does not take effect if `--analyse` is not configured.                                                                                                                                                                                                       |     Y     |      Y      |
| --l2-cache            | Optional| L2 cache data collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                            |     Y     |      Y      |
| --op-attr             | Optional| Operator attribute information collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                                |     Y     |      N      |
| --msprof-tx           | Optional| mstx dotting data collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.<br>In the PyTorch or MindSpore scenario, after this function is enabled, the mstx dotting collects the time consumed by the communication operators (domain: communication) and dataloader, and saves the time consumed by the checkpoint APIs (domain: default) by default.                                               |     Y     |      Y      |
| --mstx-domain-include | Optional| When `--msprof-tx` is enabled to collect mstx dotting data, set this parameter to specify the domain range to be collected. By default, the domain range to be collected is not configured.<br>This option is mutually exclusive with the `--mstx-domain-exclude` option. If both options are set, only the `--mstx-domain-include` option takes effect.<br>You can configure one or more domains, for example, `--mstx-domain-include domain1, domain2`.|     Y     |      Y      |
| --mstx-domain-exclude | Optional| When `--msprof-tx` is enabled to collect mstx dotting data, set this parameter to specify the domain range excluded from collection. By default, the domain range excluded from collection is not configured.<br>This option is mutually exclusive with the `--mstx-domain-include` option. If both options are set, only the `--mstx-domain-include` option takes effect.<br>You can configure one or more domains, for example, `--mstx-domain-exclude domain1, domain2`.                                                        |     Y     |      Y      |
| --data-simplification | Optional| Data simplification mode. The value can be:<br>&#8226; `true`: enables data simplification. After this function is enabled, redundant data is deleted after profile data is exported. Only the `profiler_*.json` file, `ASCEND_PROFILER_OUTPUT` directory, original profile data in the `PROF_XXX` directory, `FRAMEWORK` directory, and `logs` directory are retained to save storage space.<br>&#8226; `false`: disables data simplification.<br>The default value is `true`.             |     Y     |      Y      |
| --activities          | Optional| CPU and NPU event collection scope. The values are as follows:<br>&#8226; `CPU`: data collection switch of the framework.<br>&#8226; `NPU`: data collection switch of the CANN software stack and NPU.<br>By default, CPU and NPU events are collected concurrently. That is, `--activities CPU,NPU` is configured.                                    |     Y     |      Y      |
| --profiler-level      | Optional| Collection level of profiler. The values are as follows:<br>&#8226; `Level_none`: Does not collect data at all levels. That is, `--profiler_level` is disabled.<br>&#8226; `Level0`: Collects upper-layer application data, bottom-layer NPU data, and information about operators executed on the NPU.<br>&#8226; `Level1`: Collects the data at level 0, AscendCL data at the CANN layer, and AI Core performance metrics executed on the NPU, enables `--aic-metrics PipeUtilization`, and generates the `communication.json`, `communication_matrix.json`, and `api_statistic.csv` files of the communication operator.<br>&#8226; `Level2`: Collects the data at level 1, runtime data at the CANN layer, and AI CPU data (`data_preprocess.csv`).<br>&#8226; The default value is `Level0`.|     Y     |      Y      |
| --aic-metrics         | Optional| AI Core metrics to be collected. The values are as follows:<br>&#8226; `AiCoreNone`: Disables AI Core performance metric collection.<br>&#8226; `PipeUtilization`: percentages of time taken by compute units and MTEs.<br>&#8226; `ArithmeticUtilization`: percentages of arithmetic utilization.<br>&#8226; `Memory`: ratio of external memory read/write instructions.<br>&#8226; `MemoryL0`: ratio of internal memory L0 read/write instructions.<br>&#8226; `ResourceConflictRatio`: percentages of pipeline queue instructions.<br>&#8226; `MemoryUB`: ratio of internal memory UB read/write instructions.<br>&#8226; `L2Cache`: cache re-allocations upon missing of the read/write cache hit count.<br>&#8226; `MemoryAccess`: bandwidth of the operator's memory access on cores.<br>If `--profiler-level` is set to `Level_none` or `Level0`, the default value is `AiCoreNone`. If `--profiler-level` is set to `Level1` or `Level2`, the default value is `PipeUtilization`.|     Y     |      Y      |
| --export-type         | Optional| Type of the data analyzed and exported by the profiler. The values are as follows:<br>&#8226; `Text`: timeline and summary files in .json and .csv formats and .db files that summarize all profile data.<br>&#8226; `Db`: Only .db files that summarize all profile data are analyzed and displayed using MindStudio Insight.<br>The default value is `Text`.|     Y     |      Y      |
| --gc-detect-threshold | Optional| GC detection threshold. The value is of the Option\<f32\> type, in milliseconds. GC events are collected only when their occurrence exceeds the threshold. By default, GC detection is disabled when this option is not set.                                                                                                                                                                                                                          |     Y     |      N      |
| --host-sys            | Optional| Host-side system data to be collected. The values are as follows:<br>&#8226; `cpu`: process CPU usage<br>&#8226; `mem`: process memory usage<br>&#8226; `disk`: process disk I/O usage<br>&#8226; `network`*: network I/O usage<br>&#8226; `osrt`: process syscall and pthreadcall<br>You can set one or more types. Use commas (,) to separate multiple types, for example, `--host-sys cpu,mem`.<br>By default, this option is not set, indicating that host-side system data collection is disabled.|     Y     |      Y      |
| --sys-io              | Optional| NIC and RoCE data collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                           |     Y     |      Y      |
| --sys-interconnection | Optional| Collective communication bandwidth data (HCCS), PCIe, and inter-chip transmission bandwidth data collection switch. The value is of the action type. If this option is set, the collection is enabled. If this option is not set, the collection is disabled.                                                                                                                                                                                                  |     Y     |      Y      |

**Example**

1. Start the dynolog daemon process. For details, see [dynolog](./dynolog_instruct.md).

   ```bash
   # Enable the dynolog daemon in CLI mode.
   dynolog --enable-ipc-monitor --certs-dir /home/server_certs
   ```

2. Enable the dynolog environment variable in the training or inference job startup window.

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. Start a training or inference job.

   ```bash
   # The PyTorch optimizer or native optimizer is required in the training job.
   bash train.sh
   ```

4. Use the dyno CLI to dynamically trigger trace dump.

   ```bash
   # Example 1: Collect data of two steps starting from the 10th step, including the framework, CANN, and device data. After the collection is complete, the data is automatically analyzed and not simplified. The output path is /tmp/profile_data.
   dyno --certs-dir /home/client_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   
   # Example 2: Collect data of two steps starting from the next step, including the framework, CANN, and device data. After the collection is complete, the data is automatically analyzed and not simplified. The output path is /tmp/profile_data.
   dyno --certs-dir /home/client_certs nputrace --start-step -1 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   
   # Example 3: Collect data of two steps starting from the 10th step, including only the CANN and device data. After the collection is complete, the data is automatically analyzed and simplified. The output path is /tmp/profile_data.
   dyno --certs-dir /home/client_certs nputrace --start-step 10 --iterations 2 --activities NPU --analyse --data-simplification true --log-file /tmp/profile_data
   
   # Example 4: Collect data of two steps starting from the 10th step. Only CANN and device data is collected but not analyzed. The data is output to /tmp/profile_data.
   dyno --certs-dir /home/client_certs nputrace --start-step 10 --iterations 2 --activities NPU --log-file /tmp/profile_data
   
   # Example 5: In the multi-server scenario, send parameter information to a specific server x.x.x.x. The parameters indicate that data of two steps starting from the 10th step is collected. Only CANN and device data is collected but not analyzed. The data is output to /tmp/profile_data.
   dyno --certs-dir /home/client_certs --hostname x.x.x.x nputrace --start-step 10 --iterations 2 --activities NPU --log-file /tmp/profile_data
   ```

## Output File Description

For details about the output data format and deliverables of nputrace, see [MindSpore and PyTorch framework profile data file reference](<>).
