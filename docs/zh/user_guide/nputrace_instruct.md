# nputrace使用说明

## 简介

nputrace工具的作用是获取框架、CANN以及device的详细性能数据。

## 使用前准备

安装msMonitor工具。详情请参见《[msMonitor工具安装指南](../install_guide/msmonitor_install_guide.md)》，推荐使用下载软件包安装。

## nputrace功能介绍

**功能说明**

执行性能数据采集。

**注意事项**

nputrace作为dyno命令的子命令，执行命令时需配置--certs-dir参数，且--certs-dir参数配置的值须与[dyno](dyno_instruct.md)和[dynolog](dynolog_instruct.md)中的--certs-dir值保持一致。

**命令格式**

```bash
dyno --certs-dir <CERT_DIR> nputrace [options]
```

`CERT_DIR`配置为证书路径，如果不使用TLS证书密钥，则设置为NO_CERTS；[options]为nputrace功能的参数，详细介绍如下**参数说明**。

**参数说明**

| 子命令                   |  可选/必选  | 说明                                                                                                                                                                                                                                                                                 | PyTorch支持 | MindSpore支持 |
|-----------------------|:----------:|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|:---------:|:-----------:|
| --job-id              | 可选 | 采集任务的ID，u64类型，默认值0。dynolog原生参数。                                                                                                                                                                                                                                              |     N     |      N      |
| --pids                | 可选 | 采集任务的PID列表，String类型，多个PID用逗号分隔，默认值0。dynolog原生参数。                                                                                                                                                                                                                              |     N     |      N      |
| --process-limit       | 可选 | 最大采集进程的数量，u64类型，默认值3。dynolog原生参数。                                                                                                                                                                                                                                                  |     N     |      N      |
| --profile-start-time  | 可选 | 用于同步采集的Unix时间戳，u64类型，单位ms，默认值0。dynolog原生参数。                                                                                                                                                                                                                                        |     N     |      N      |
| --duration-ms         | 可选 | 采集的周期，u64类型，单位ms，默认值500。dynolog原生参数。                                                                                                                                                                                                                                             |     N     |      N      |
| --iterations          | 必选 | 采集总迭代数，i64类型，仅支持配置为正整数。dynolog原生参数，需与--start-step参数同时指定。                                                                                                                                                                                                              |     Y     |      Y      |
| --log-file            | 必选 | 采集落盘的路径，String类型。                                                                                                                                                                                                                                                               |     Y     |      Y      |
| --start-step          | 必选 | 开始采集的迭代数，i64类型，仅支持配置为正整数或-1，设置为-1时表示从下一个step开始采集。                                                                                                                                                                                                                           |     Y     |      Y      |
| --record-shapes       | 可选 | 算子的InputShapes和InputTypes采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                            |     Y     |      Y      |
| --profile-memory      | 可选 | 算子内存信息采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                                 |     Y     |      Y      |
| --with-stack          | 可选 | Python调用栈采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                              |     Y     |      Y      |
| --with-flops          | 可选 | 算子flops采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                                |     Y     |      N      |
| --with-modules        | 可选 | modules层级的Python调用栈采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                    |     Y     |      N      |
| --analyse             | 可选 | 采集后自动解析开关，action类型，配置该参数表示开启自动解析，默认未配置表示不自动解析。                                                                                                                                                                                                          |     Y     |      Y      |
| --async-mode          | 可选 | 异步解析开关，action类型，配置该参数表示开启异步解析，默认未配置表示同步解析。未配置--analyse的情况下不生效。                                                                                                                                                                                                        |     Y     |      Y      |
| --l2-cache            | 可选 | L2 Cache数据采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                             |     Y     |      Y      |
| --op-attr             | 可选 | 算子属性信息采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                                 |     Y     |      N      |
| --msprof-tx           | 可选 | mstx打点数据采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。<br/>PyTorch或MindSpore场景下，该开关开启后，mstx打点数据默认采集通信算子（domain为communication）和dataloader耗时、保存检查点接口耗时（domain为default）。                                                |     Y     |      Y      |
| --mstx-domain-include | 可选 | 开启--msprof-tx采集mstx打点数据的情况下，配置该参数，设置实际采集的domain范围，String类型。默认未配置实际采集的domain范围。<br>与--mstx-domain-exclude参数互斥，若同时设置，则只有--mstx-domain-include生效。<br/>可配置一个或多个domain，例如：--mstx-domain-include domain1,domain2。 |     Y     |      Y      |
| --mstx-domain-exclude | 可选 | 开启--msprof-tx采集mstx打点数据的情况下，配置该参数，设置实际不采集的domain范围，String类型。默认未配置实际不采集的domain范围。<br/>与--mstx-domain-include参数互斥，若同时设置，则只有--mstx-domain-include生效。<br/>可配置一个或多个domain，例如：--mstx-domain-exclude domain1,domain2。 |     Y     |      Y      |
| --rank-list           | 可选 | 指定需要采集的rank列表，String类型。多个rank使用逗号分隔，例如：--rank-list 0,1,2,3。未配置时采集所有rank的数据。 |     Y     |      N      |
| --data-simplification | 可选 | 数据精简模式，取值为：<br/>&#8226; true：表示开启数据精简，开启后将在导出性能数据后删除多余数据，仅保留profiler_*.json文件、ASCEND_PROFILER_OUTPUT目录、PROF_XXX目录下的原始性能数据、FRAMEWORK目录和logs目录，以节省存储空间。<br/>&#8226; false：表示关闭数据精简。<br/>默认值为true。              |     Y     |      Y      |
| --activities          | 可选 | 控制CPU、NPU事件采集范围，取值为：<br/>&#8226; CPU：框架侧数据采集的开关。<br/>&#8226; NPU：CANN软件栈及NPU数据采集的开关。<br/>默认情况下CPU、NPU事件采集同时开启，即配置为--activities CPU,NPU。                                     |     Y     |      Y      |
| --profiler-level      | 可选 | 控制Profiler的采集等级，取值为：<br/>&#8226; Level_none：不采集所有Level层级控制的数据，即关闭--profiler_level。<br/>&#8226; Level0：采集上层应用数据、底层NPU数据以及NPU上执行的算子信息。<br/>&#8226; Level1：效果为在Level0的基础上，多采集CANN层AscendCL数据和NPU上执行的AI Core性能指标信息、开启--aic-metrics PipeUtilization、生成通信算子的communication.json和communication_matrix.json以及api_statistic.csv文件。<br/>&#8226; Level2：效果为在Level1的基础上多采集CANN层Runtime数据以及AI CPU（data_preprocess.csv文件）数据。<br/>&#8226; 默认值为Level0。 |     Y     |      Y      |
| --aic-metrics         | 可选 | AI Core的性能指标采集项，取值为：<br/>&#8226; AiCoreNone：关闭AI Core的性能指标采集。<br/>&#8226; PipeUtilization：计算单元和搬运单元耗时占比。<br/>&#8226; ArithmeticUtilization：各种计算类指标占比统计。<br/>&#8226; Memory：外部内存读写类指令占比。<br/>&#8226; MemoryL0：内部L0内存读写类指令占比。<br/>&#8226; ResourceConflictRatio：流水线队列类指令占比。<br/>&#8226; MemoryUB：内部UB内存读写类指令占比。<br/>&#8226; L2Cache：读写cache命中次数和缺失后重新分配次数。<br/>&#8226; MemoryAccess：算子在核上访存的带宽数据量。<br/>当--profiler-level设置为Level_none或Level0，默认值为AiCoreNone，当--profiler-level设置为Level1或Level2，默认值为PipeUtilization。 |     Y     |      Y      |
| --export-type         | 可选 | profiler解析导出数据的类型，取值为：<br/>&#8226; Text：表示解析为.json和.csv格式的timeline和summary文件以及汇总所有性能数据的.db格式文件。<br/>&#8226; Db：表示仅解析为汇总所有性能数据的.db格式文件，使用MindStudio Insight工具展示。<br/>默认值为Text。 |     Y     |      Y      |
| --gc-detect-threshold | 可选 | GC检测阈值，Option\<f32\>类型，单位ms，只采集超过阈值的GC事件。默认不设置时不开启GC检测。                                                                                                                                                                                                                           |     Y     |      N      |
| --host-sys            | 可选 | 采集host侧系统数据，取值为：<br/>&#8226; cpu：进程级别的CPU利用率。<br/>&#8226; mem：进程级别的内存利用率。<br/>&#8226; disk：进程级别的磁盘I/O利用率。<br/>&#8226; network：系统级别的网络I/O利用率。<br/>&#8226; osrt：进程级别的syscall和pthreadcall。<br/>可以设置单个或多个，多个类型以逗号分隔，例如：--host-sys cpu,mem。<br/>默认未配置，表示未开启Host侧系统数据采集。 |     Y     |      Y      |
| --sys-io              | 可选 | NIC、ROCE数据采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                            |     Y     |      Y      |
| --sys-interconnection | 可选 | 集合通信带宽数据（HCCS）、PCIe、片间传输带宽数据采集开关，action类型，配置该参数表示开启采集，默认未配置表示不采集。                                                                                                                                                                                                   |     Y     |      Y      |

**使用示例**

1. 启动dynolog daemon进程，详细介绍请参见[dynolog](./dynolog_instruct.md)。

   ```bash
   # 命令行方式开启dynolog daemon
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs
   ```

2. 在训练或推理任务拉起窗口使能dynolog环境变量。

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. 启动训练或推理任务。

   ```bash
   # 训练任务中需要使用PyTorch的优化器/继承原生优化器
   bash train.sh
   ```

4. 使用dyno CLI动态触发trace dump。

   ```bash
   # 示例1：从第10个step开始采集，采集2个step，采集框架、CANN和device数据，同时采集完后自动解析以及解析完成不做数据精简，落盘路径为/tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data

   # 示例2：从下一个step开始采集，采集2个step，采集框架、CANN和device数据，同时采集完后自动解析以及解析完成不做数据精简，落盘路径为/tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step -1 --iterations 2 --activities CPU,NPU --analyse --data-simplification false --log-file /tmp/profile_data

   # 示例3：从第10个step开始采集，采集2个step，只采集CANN和device数据，同时采集完后自动解析以及解析完成后开启数据精简，落盘路径为/tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities NPU --analyse --data-simplification true --log-file /tmp/profile_data

   # 示例4：从第10个step开始采集，采集2个step，只采集CANN和device数据，只采集不解析，落盘路径为/tmp/profile_data
   dyno --certs-dir /home/ssl_certs nputrace --start-step 10 --iterations 2 --activities NPU --log-file /tmp/profile_data

   # 示例5：多机场景下向特定机器x.x.x.x发送参数信息，参数表示从第10个step开始采集，采集2个step，只采集CANN和device数据，只采集不解析，落盘路径为/tmp/profile_data
   dyno --certs-dir /home/ssl_certs --hostname x.x.x.x nputrace --start-step 10 --iterations 2 --activities NPU --log-file /tmp/profile_data
   ```

## 输出结果文件说明

nputrace落盘的数据格式和交付件介绍请参见[MindSpore&PyTorch框架性能数据文件参考](https://www.hiascend.com/document/detail/zh/mindstudio/830/T&ITools/Profiling/atlasprofiling_16_0204.html)。
