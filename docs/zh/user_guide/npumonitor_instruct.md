# npu-monitor使用说明

## 简介

npu-monitor工具的作用是轻量常驻后台，负责监测关键算子耗时。

## 使用前准备

npu-monitor通过dyno CLI中的npu-monitor子命令开启：

```bash
dyno --certs-dir <CERT_DIR> npu-monitor [SUBCOMMANDS]
```

**约束**

- dyno和dynolog中--certs-dir传入参数值须保持一致。
- `CERT_DIR`可传入证书路径，如果不使用TLS证书密钥，设置为NO_CERTS。

## npu-monitor功能介绍

**功能说明**

开启npu-monitor性能监测。

**命令格式**

```bash
dyno npu-monitor [SUBCOMMANDS] --help
```

npu-monitor的SUBCOMMANDS（子命令）选项如下。

**参数说明**

| 子命令                   | 参数类型 | 说明                                                                                                                                                  | PyTorch支持 | MindSpore支持 |    是否必选     |
|-----------------------|-------|------------------------------------------------------------------------------------------------------------------------------------------------------|:---------:|:-----------:|:-----------:|
| --npu-monitor-start   | action | 开启性能监测，设置参数后生效，默认不生效。                                                                                                                              | Y | Y | N |
| --npu-monitor-stop    | action | 停止性能监测，设置参数后生效，默认不生效。                                                                                                                               | Y | Y | N |
| --report-interval-s   | u32 | 性能监测数据上报周期，单位s，需要在启动时设置。默认值60。                                                                                                                      | Y | Y | N |
| --duration            | f32 | 性能监测数据采集时长，单位s，需要在启动时设置。默认值0.0，表示不限制采集时长。仅可在npu_monitor启动时设置，运行期间不支持修改。                                                                                        | Y | Y | N |
| --mspti-activity-kind | String | 性能监测数据上报数据类型，可以设置单个或多个，多个类型以逗号分隔，每次设置时刷新全局上报类型。可选值范围[`Marker`，`Kernel`，`API`，`Hccl`，`Memory`，`MemSet`，`MemCpy`，`Communication`，`AclAPI`，`NodeAPI`，`RuntimeAPI`] , 默认值`Marker`。 | Y | Y | N |
| --log-file            | String | 性能数据采集落盘的路径，当前仅支持`mspti-activity-kind`设置为`Marker`、`Kernel`、`API`、`Communication`、`AclAPI`、`NodeAPI`、`RuntimeAPI`，7种类型数据的导出，落盘数据格式可选为DB、Jsonl（详见`export-type`参数说明），默认值为空，表示不落盘。 | Y | Y | N |
| --export-type         | String | 性能数据采集落盘的格式，仅在用户设置了`log-file`参数后生效，可选值范围[`DB`, `Jsonl`]，默认值`DB`。仅可在npu_monitor启动时设置，运行期间不支持修改。<br> **1.** 若设置为`DB`，则落盘数据为DB格式，落盘文件名为`msmonitor_{process_id}_{timestamp}_{rank_id}.db`，DB内容说明请参见[msprof导出db格式数据说明](https://www.hiascend.com/document/detail/zh/canncommercial/83RC1/devaids/Profiling/atlasprofiling_16_1144.html)，可使用[MindStudio Insight](https://www.hiascend.com/document/detail/zh/mindstudio/82RC1/GUI_baseddevelopmenttool/msascendinsightug/Insight_userguide_0002.html)工具进行可视化呈现（MindStudio Insight暂不支持呈现单进程多卡场景采集的msmonitor.db数据） <br> **2.** 若设置为`Jsonl`，则落盘数据为Jsonl格式，落盘文件名为`msmonitor_{process_id}_{timestamp}_{rank_id}.jsonl`，Jsonl文件每行包含一条完整的Json格式的性能数据，支持设置以下环境变量对落盘过程进行调节 <br> **MSMONITOR_JSONL_BUFFER_CAPACITY**：设置落盘 RingBuffer 大小，该参数必须为2的幂次（$2^{n}$），默认值 524288（$2^{19}$），支持的设置范围为 [8192，2097152]（即 [$2^{13}$，$2^{21}$]） <br> **MSMONITOR_JSONL_MAX_DUMP_INTERVAL**：设置落盘最长时间间隔（单位：ms），当前时间与上次落盘的间隔超过该阈值时，将自动触发落盘，默认值 30000ms，最小值限制为 1000ms <br> **MSMONITOR_JSONL_ROTATE_LOG_LINES**：设置单个 Jsonl 文件的 Json 数据条数上限，超出该阈值将新建文件落盘。默认值 10000，支持设置范围为 [100, 500000] <br> **MSMONITOR_JSONL_ROTATE_LOG_FILES**：设置单次采集的 Jsonl 文件落盘数量，超出该阈值时将删除最早落盘的文件。默认值 -1（不开启此功能），手动设置时最小值限制为 2 | Y | Y | N |
| --json-rotate-log-lines | String | Jsonl 落盘时单个 Jsonl 文件的 Json 数据条数上限，仅在 --log-file 与 --export-type Jsonl 配置时生效。该参数默认不传入，优先级高于环境变量 `MSMONITOR_JSONL_ROTATE_LOG_LINES`；若命令行和环境变量均未设置，则使用默认值 `10000`。手动设置时必须传入范围为 [100, 500000] 的正整数。 | Y | Y | N |
| --json-rotate-log-files | String | Jsonl 落盘时单次采集保留的 Jsonl 文件数量，超出该阈值时删除最早落盘的文件，仅在 --log-file 与 --export-type Jsonl 配置时生效。该参数默认不传入，优先级高于环境变量 `MSMONITOR_JSONL_ROTATE_LOG_FILES`；若命令行和环境变量均未设置，则使用默认值 `-1`，表示不启用文件数量清理。手动设置时必须传入 -1 或大于等于 2 的正整数。 | Y | Y | N |
| --filter              | String | 按照想采集的数据名筛选性能数据。不同数据类型以分号分隔，不同数据名以逗号分隔，数据类型和名称之间以冒号分隔。支持模糊匹配，无需配置完整名称，只需要配置关键词即可。配置值包含分号时，需用双引号包裹整个值。配置方式示例：`--filter "<activity_kind>:<data>[,<data>][;<activity_kind>:<data>[,<data>]]"`。activity_kind可选值范围[`Marker`，`Kernel`，`API`，`Communication`，`AclAPI`，`NodeAPI`，`RuntimeAPI`]，默认不筛选，保留所有数据。 | Y | Y | N |

1. 启动dynolog daemon进程，详细介绍请参见[dynolog](./dynolog_instruct.md)。

   ```bash
   # 命令行方式开启dynolog daemon
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs

   # 如需使用Tensorboard展示数据，传入参数--metric_log_dir用于指定Tensorboard文件落盘路径
   # 示例：
   dynolog --enable-ipc-monitor --certs-dir /home/ssl_certs --metric_log_dir /tmp/metric_log_dir
   ```

2. 配置dynolog环境变量。

   ```bash
   export MSMONITOR_USE_DAEMON=1
   ```

3. （可选）配置msMonitor日志路径，默认路径为当前目录下的msmonitor_log。

   ```bash
   export MSMONITOR_LOG_PATH=<LOG PATH>
   # 示例：
   export MSMONITOR_LOG_PATH=/tmp/msmonitor_log
   ```

4. 设置LD_PRELOAD使能MSPTI。

   ```bash
   export LD_PRELOAD=<CANN Toolkit安装路径>/cann/lib64/libmspti.so
   # 示例：
   export LD_PRELOAD=/usr/local/Ascend/cann/lib64/libmspti.so
   ```

5. 启动训练或推理任务。

   ```bash
   # 训练任务中需要使用pytorch的优化器/继承原生优化器
   bash train.sh
   ```

6. 使用dyno CLI启动npu-monitor。

   ```bash
   # 示例1：开启性能监测，使用默认配置
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start

   # 示例2：暂停性能监测
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-stop

   # 示例3：性能监测过程中修改配置
   # 上报周期30s, 上报数据类型Marker和Kernel，保留类型为Kernel且算子名称中包含“Mul”关键词的数据
   dyno --certs-dir /home/ssl_certs npu-monitor --report-interval-s 30 --mspti-activity-kind Marker,Kernel --filter Kernel:Mul

   # 示例4：性能监测开启时修改配置
   # 上报周期30s, 上报数据类型Marker和Kernel，保留类型为Kernel且算子名称中包含“Mul”关键词的数据
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel --filter Kernel:Mul

   # 示例5：性能监测开启时修改配置，开启数据采集落盘
   # 数据落盘路径为/tmp/msmonitor_db，落盘周期为30s，采集数据类型为Marker，Kernel，Communication
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel,Communication --log-file /tmp/msmonitor_db

   # 示例6：以 Jsonl 格式落盘，并通过命令行参数设置 Jsonl 文件轮转配置
   dyno --certs-dir /home/ssl_certs npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel,Communication --log-file /tmp/msmonitor_jsonl --export-type Jsonl --json-rotate-log-lines 10000 --json-rotate-log-files 5

   # 示例7：多机场景下性能监测开启时修改配置
   # 多机场景下向特定机器x.x.x.x发送参数信息，参数表示上报周期30s, 上报数据类型Marker和Kernel
   dyno --certs-dir /home/ssl_certs --hostname x.x.x.x npu-monitor --npu-monitor-start --report-interval-s 30 --mspti-activity-kind Marker,Kernel
   ```

## 输出结果文件说明

观测TensorBoard上报数据。

```bash
# 请确保安装了TensorBoard
pip install tensorboard

# 运行TensorBoard
tensorboard --logdir=<metric_log_dir> # metric_log_dir为使用示例dynolog命令行中--metric_log_dir参数指定的路径

# 从浏览器访问http://localhost:6006即可看到对应可视化图表, 其中localhost为服务器的ip地址，6006为TensorBoard默认端口
```

TensorBoard具体使用参数见<https://github.com/tensorflow/tensorboard>
