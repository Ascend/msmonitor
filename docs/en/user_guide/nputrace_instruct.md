# nputrace Usage Guide

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T08:31:35.592Z pushedAt=2026-08-12T08:32:53.776Z -->

## Introduction

The nputrace tool is used to obtain detailed performance data of the framework, CANN, and device.

## Preparations

Install the msMonitor tool. For details, see *[msMonitor Installation Guide](../install_guide/msmonitor_install_guide.md)*. It is recommended to install using the downloaded software package.

## nputrace Feature Description

**Feature Description**

Performs performance data collection.

**Precautions**

nputrace is a subcommand of the dyno command. When executing the command, you must configure the --certs-dir parameter, and the value of --certs-dir must be consistent with the --certs-dir values in [dyno](dyno_instruct.md) and [dynolog](dynolog_instruct.md).

**Command Format**

```bash
dyno --certs-dir <CERT_DIR> nputrace [options]
```

`CERT_DIR` is configured as the certificate path. If TLS certificate keys are not used, set it to NO_CERTS. [options] are the parameters of the nputrace feature, detailed in **Parameter Description** below.

**Parameter Description**

| Subcommand                   | Optional/Required | Description                                                                                                                                                                                                                                                                                 | PyTorch Support | MindSpore Support |
|-----------------------|:----------:|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:---------:|:-----------:|
| --job-id              | Optional | ID of the collection task, u64 type, default value 0. dynolog Native Parameter.                                                                                                                                                                                                                                              |     N     |      N      |
| --pids                | Optional | PID list of the collection task, String type, multiple PIDs separated by commas, default value 0. dynolog Native Parameter.                                                                                                                                                                                                                              |     N     |      N      |
| --process-limit       | Optional | Maximum number of processes to collect, u64 type, default value 3. dynolog Native Parameter.                                                                                                                                                                                                                                                  |     N     |      N      |
| --profile-start-time  | Optional | Unix timestamp for synchronized collection, u64 type, unit: ms, default value 0. dynolog Native Parameter.                                                                                                                                                                                                                                        |     N     |      N      |
| --duration-ms         | Optional | Collection period, u64 type, unit: ms, default value 500. dynolog Native Parameter.                                                                                                                                                                                                                                             |     N     |      N      |
| --iterations          | Required | Total number of collection iterations, i64 type, only positive integers are supported. dynolog Native Parameter, must be specified together with the --start-step parameter.                                                                                                                                                                                                              |     Y     |      Y      |
| --log-file            | Required | Path for writing collection data to disk, String type.                                                                                                                                                                                                                                                               |     Y     |      Y      |
| --start-step          | Required | Iteration number at which collection starts, i64 type, only positive integers or -1 are supported. When set to -1, collection starts from the next step.                                                                                                                                                                                                                           |     Y     |      Y      |
| --record-shapes       | Optional | Collection Switch for operator InputShapes and InputTypes, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                            |     Y     |      Y      |
| --profile-memory      | Optional | Collection Switch for operator memory information, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                                 |     Y     |      Y      |
| --with-stack          | Optional | Collection Switch for Python Call Stack, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                              |     Y     |      Y      |
| --with-flops          | Optional | Collection Switch for operator flops, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                                |     Y     |      N      |
| --with-modules        | Optional | Collection Switch for module-level Python Call Stack, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                    |     Y     |      N      |
| --analyse             | Optional | Switch for automatic parsing after collection, action type. Specifying this parameter enables automatic parsing. Not specified by default, meaning no automatic parsing.                                                                                                                                                                                                          |     Y     |      Y      |
| --async-mode          | Optional | Switch for asynchronous parsing, action type. Specifying this parameter enables asynchronous parsing. Not specified by default, meaning synchronous parsing. Does not take effect if --analyse is not specified.                                                                                                                                                                                                        |     Y     |      Y      |
| --l2-cache            | Optional | Collection Switch for L2 Cache Data, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                             |     Y     |      N      |
| --op-attr             | Optional | Collection Switch for operator attribute information, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                                 |     Y     |      N      |
| --msprof-tx           | Optional | Collection Switch for mstx Trace Data, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.<br/>In PyTorch or MindSpore scenarios, when this switch is enabled, mstx Trace Data is collected by default for communication operators (domain: communication), dataloader elapsed time, and checkpoint saving interface elapsed time (domain: default).                                                |     Y     |      Y      |
| --mstx-domain-include | Optional | When --msprof-tx is enabled for collecting mstx Trace Data, specify this parameter to set the actual domain Range for collection, String type. Not specified by default, meaning the actual collection domain Range is not set.<br>Mutually exclusive with the --mstx-domain-exclude parameter. If both are set, only --mstx-domain-include takes effect.<br/>One or more domains can be configured, for example: --mstx-domain-include domain1,domain2. |     Y     |      Y      |
| --mstx-domain-exclude | Optional | When --msprof-tx is enabled for collecting mstx Trace Data, specify this parameter to set the domain Range to exclude from collection, String type. Not specified by default, meaning no domain Range is excluded.<br/>Mutually exclusive with the --mstx-domain-include parameter. If both are set, only --mstx-domain-include takes effect.<br/>One or more domains can be configured, for example: --mstx-domain-exclude domain1,domain2. |     Y     |      Y      |
| --rank-list           | Optional | Specifies the list of ranks to collect, String type. Multiple ranks are separated by commas, for example: --rank-list 0,1,2,3. When not specified, Data from all ranks is collected. |     Y     |      N      |
| --data-simplification | Optional | Data Reduction mode. Values:<br/>&#8226; true: Enables Data Reduction. After exporting performance data, redundant data is deleted, retaining only the profiler_*.json File, the ASCEND_PROFILER_OUTPUT directory, the original performance data in the PROF_XXX directory, the FRAMEWORK directory, and the logs directory to save storage space.<br/>&#8226; false: Disables Data Reduction.<br/>Default Value: true.              |     Y     |      Y      |
| --activities          | Optional | Controls the collection Range of CPU and NPU events. Values:<br/>&#8226; CPU: Switch for framework-side Data Collection.<br/>&#8226; NPU: Switch for CANN software stack and NPU Data Collection.<br/>By default, both CPU and NPU event collection are enabled, i.e., configured as --activities CPU,NPU.                                     |     Y     |      Y      |
| --profiler-level      | Optional | Controls the Profiler collection level. Values:<br/>&#8226; Level_none: Does not collect any data controlled by level hierarchy, i.e., disables --profiler-level.<br/>&#8226; Level0: Collects upper-layer app data, underlying NPU data, and operator information executed on the NPU.<br/>&#8226; Level1: On top of Level0, additionally collects CANN-layer AscendCL data, AI Core performance metric information executed on the NPU, enables --aic-metrics PipeUtilization, and generates communication.json, communication_matrix.json, and api_statistic.csv files for communication operators.<br/>&#8226; Level2: On top of Level1, additionally collects CANN-layer Runtime data and AI CPU (data_preprocess.csv File) data.<br/>&#8226; Default Value: Level0. |     Y     |      Y      |
| --aic-metrics         | Optional | AI Core performance metric collection items. Values:<br/>&#8226; AiCoreNone: Disables AI Core performance metric collection.<br/>&#8226; PipeUtilization: Ratio of time consumed by compute units and transfer units.<br/>&#8226; ArithmeticUtilization: Statistics on the ratio of various compute metrics.<br/>&#8226; Memory: Ratio of external memory read/write instructions.<br/>&#8226; MemoryL0: Ratio of internal L0 memory read/write instructions.<br/>&#8226; ResourceConflictRatio: Ratio of pipeline queue instructions.<br/>&#8226; MemoryUB: Ratio of internal UB memory read/write instructions.<br/>&#8226; L2Cache: Number of read/write cache hits and reallocations after misses.<br/>&#8226; MemoryAccess: Bandwidth Data volume of operator on-core memory access.<br/>When --profiler-level is set to Level_none or Level0, the Default Value is AiCoreNone. When --profiler-level is set to Level1 or Level2, the Default Value is PipeUtilization. |     Y     |      Y      |
| --export-type         | Optional | Type of profiler parsed export data. Values:<br/>&#8226; Text: Parses into timeline and summary files in .json and .csv formats, as well as a .db format File that aggregates all performance data.<br/>&#8226; Db: Parses only into a .db format File that aggregates all performance data, displayed using the MindStudio Insight tool.<br/>Default Value: Text. |     Y     |      Y      |
| --gc-detect-threshold | Optional | GC detection threshold, Option\<f32\> type, unit: ms. Only GC events exceeding the threshold are collected. When not set by default, GC detection is disabled.                                                                                                                                                                                                                           |     Y     |      N      |
| --host-sys            | Optional | Collects host-side system data. Values:<br/>&#8226; cpu: Process Level CPU Utilization Rate.<br/>&#8226; mem: Process Level memory Utilization Rate.<br/>&#8226; disk: Process Level disk I/O Utilization Rate.<br/>&#8226; network: System-level network I/O Utilization Rate.<br/>&#8226; osrt: Process Level syscall and pthreadcall.<br/>A single or multiple types can be set, separated by commas, for example: --host-sys cpu,mem.<br/>Not specified by default, meaning host-side system Data Collection is disabled. |     Y     |      Y      |
| --sys-io              | Optional | Collection Switch for NIC and ROCE data, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                            |     Y     |      Y      |
| --sys-interconnection | Optional | Collection Switch for collective communication Bandwidth Data (HCCS), PCIe, and inter-chip transfer Bandwidth Data, action type. Specifying this parameter enables collection. Not specified by default, meaning no collection.                                                                                                                                                                                                   |     Y     |      Y      |

**Usage Examples**

1. Start the dynolog daemon process. For details, see [dynolog](./dynolog_instruct.md).

   ```bash
   # Start the dynolog daemon from the command line.
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs
   ```

2. Enable the dynolog environment variable in the window where the training or inference task is launched.

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. Start the training or inference task.

   ```bash
   # The training task must use the PyTorch optimizer or inherit the native optimizer.
   bash train.sh
   ```

4. Use the dyno CLI to dynamically trigger trace dump.

   ```bash
   # Example 1: Start collection from the 10th step, collect 2 steps, collect framework, CANN, and device data, perform automatic analysis after collection, do not perform data reduction after analysis, and set the dump path to /tmp/profile_data.
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   
   # Example 2: Start collection from the next step, collect 2 steps, collect framework, CANN, and device data, automatically analyze after collection and disable Data Reduction after analysis, with the dump path set to /tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step -1 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data
   
   # Example 3: Start collection from step 10, collect 2 steps, collect only CANN and device data, automatically analyze after collection and enable Data Reduction after analysis, with the dump path set to /tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities NPU --analyse --data-simplification true --log-file /tmp/profile_data
   
   # Example 4: Start collection from step 10, collect 2 steps, collect only CANN and device data, collect only without analysis, with the dump path set to /tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities NPU --log-file /tmp/profile_data
   
   # Example 5: In a multi-node scenario, send parameter information to a specific machine x.x.x.x. The parameters indicate starting collection from step 10, collecting 2 steps, collecting only CANN and device data, collecting only without analysis, with the dump path set to /tmp/profile_data
   dyno --certs-dir /home/ssl_certs --hostname x.x.x.x nputrace --start-step 10 --iterations 2 --activities NPU --log-file /tmp/profile_data
   ```

## Output Result File Description

The data format and deliverables dumped by nputrace include PyTorch and MindSpore framework data. For details, see the [Output Result File Description](https://gitcode.com/Ascend/pytorch/blob/v2.7.1-26.1.0/docs/en/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md#output-result-file-description) of the Ascend PyTorch tuning tool.
