# msmonitor使用说明（beta）

## 简介

`msmonitor` 是 msMonitor 的命令行入口，负责拉起并管理两个常驻后台进程，并把采集配置下发给训练进程。

| 进程 | 作用 |
| --- | --- |
| collector（采集控制进程） | 应答训练进程的配置查询，只维护内存中的采集配置。 |
| processor（数据处理进程） | 扫描采集结果目录，按 rank 解析并输出 trace 结果文件与 metric 结果并上报；默认两者都输出。 |

msmonitor 独立完成采集配置的下发与进程管理，训练进程侧无需修改代码。

## 使用前准备

启动服务前训练侧需配置以下环境变量：

| 环境变量 | 说明 |
| --- | --- |
| MSMONITOR_USE_DAEMON | 设为1，开启 msMonitor 功能，训练进程启动前设置。 |
| MSMONITOR_LOG_PATH | 日志目录。CLI 未设置时默认为 `<output-dir>/log`，设置后以其为准；训练进程内 msMonitor 自身的 glog 日志同样读取该变量，两者各自独立生效，若在同一个 shell 中导出，两个进程会写到同一目录。 |

## 功能介绍

**命令格式**

```bash
msmonitor [OPTIONS] [SUBCOMMAND_ARGS]
```

`[OPTIONS]` 为 msmonitor 的可选参数和子命令，请参见[参数说明](#options)；`[SUBCOMMAND_ARGS]` 为对应子命令的参数。

**注意事项**

1. collector 在 Linux 账户主目录下监听 `dynolog_<hostUid>.sock`。训练进程需能通过相同的绝对路径访问该 socket。容器部署时，可将 collector 放在训练容器内，或让两个容器在相同路径共享 socket 所在目录。同一账户下不要同时启动占用该 socket 的 dynolog daemon。
2. `msmonitor start` 只负责拉起进程，不会开始采集；必须再执行 `msmonitor collect start` 才会向训练进程下发采集配置。
3. 状态目录固定为 `~/.msmonitor`，不支持通过参数修改；如需调整日志位置请使用 `MSMONITOR_LOG_PATH`。

**参数说明**<a name="options"></a>

| 参数名 | 可选/必选 | 说明 |
| --- | --- | --- |
| start | 可选 | 拉起 collector 与 processor，不下发采集配置，详细介绍请参见[启动服务](#启动服务)。 |
| collect start | 可选 | 向训练进程下发采集配置，collector 未运行时自动拉起，详细介绍请参见[下发采集配置](#下发采集配置)。 |
| collect stop | 可选 | 下发停止配置，collector 保持运行，详细介绍请参见[停止采集](#停止采集)。 |
| stop | 可选 | 停止 processor 与 collector，详细介绍请参见[停止服务](#停止服务)。 |
| -h, --help | 可选 | 获取 `msmonitor` 命令的使用帮助。 |

### 启动服务

```bash
msmonitor start --output-dir /data/msmonitor --monitor-target trace
```

`start` 的参数如下：

| 参数名 | 可选/必选 | 说明 |
| --- | --- | --- |
| --output-dir | 必选 | 采集结果目录，自动解析为绝对路径，同时作为 processor 的输入目录；后续 `collect start` 复用该值。 |
| --job-id | 可选 | 作业标识，供数据中心区分任务；后续 `collect start` 复用该值。 |
| --trace-steps-per-file | 可选 | 每个 rank 的单个 Chrome Trace 文件最多包含的 step 数，默认 5。 |
| --trace-keep-files | 可选 | 本次运行中每个 rank worker 保留的最近 trace 文件数，默认值 100，0 表示不清理。 |
| --remote-write-url | metric 必填 | 上报指标的完整 HTTPS URL，含租户写入路径。 |
| --token-file | 可选 | 认证 token 文件路径，与 `--token` 二选一，两者同时指定会报错。 |
| --token | 可选 | 明文认证 token，与 `--token-file` 二选一，两者同时指定会报错；无需在每台机器上准备 token 文件。 |
| --ca-file | 可选 | 私有 CA 的公共证书路径。省略时使用系统信任库。 |
| --monitor-target | 可选 | `trace` 导出本地 trace 文件，`metric` 通过 Remote Write 上报。默认 `trace,metric`。启用 metric 时提供 `--remote-write-url` 和 `--token-file` 或 `--token`。 |

命令执行成功后会打印 collector 的 pid、采集结果目录与日志路径。

> [!NOTE] Note
>
> * `start` 只执行一次：实例已在运行时再次执行只打印现有 pid 与日志路径，不会拉起或补齐任何进程；若传入的 `--output-dir` 与运行时不一致，会提示先执行 `msmonitor stop`。
> * `--output-dir` 不能包含 `=` 或换行，也不能与状态目录 `~/.msmonitor` 相同。

### 下发采集配置

```bash
# 按默认采集类型（Marker、Communication、Kernel）采集
msmonitor collect start

# 采集 Kernel 类数据，上报周期10s，单文件最多5000行，保留5份
msmonitor collect start --mspti-activity-kind Kernel \
    --report-interval-s 10 --json-rotate-log-lines 5000 --json-rotate-log-files 5
```

`collect start` 的参数如下：

| 参数名 | 可选/必选 | 说明 |
| --- | --- | --- |
| --mspti-activity-kind | 可选 | 采集数据类型，多个值以逗号分隔，默认值Marker,Communication,Kernel，区分大小写。<br>可选值：Marker、Kernel、Communication、API、AclAPI、NodeAPI、RuntimeAPI。 |
| --report-interval-s | 可选 | 采集上报周期，单位秒，默认值60，取值范围[0, 4294967295]，0表示沿用训练侧默认值60。 |
| --json-rotate-log-lines | 可选 | 单个JSONL文件的最大行数，取值范围[100, 500000]，默认值10000。<br>优先级：本参数 > 环境变量 `MSMONITOR_JSONL_ROTATE_LOG_LINES` > 默认值。 |
| --json-rotate-log-files | 可选 | 保留的JSONL文件份数，取值为-1或大于等于2，-1表示不清理，默认值-1。<br>优先级：本参数 > 环境变量 `MSMONITOR_JSONL_ROTATE_LOG_FILES` > 默认值。 |

> [!NOTE] Note
>
> * `collect start` 必须先执行过 `msmonitor start`：采集结果目录与 `--job-id` 复用 `start` 时保存的值，未执行过会提示配置错误。
> * 下发配置时训练进程需已完成注册，否则其轮询线程已因注册失败退出，不会再取到本次配置。
> * 本工具独立使用，请勿同时使用 `npu-monitor` 和 `nputrace`。

### 停止采集

```bash
msmonitor collect stop
```

`collect stop` 只下发停止配置，collector 继续常驻，保证任意轮询间隔的 rank 都能拿到停止配置。各 rank 在自己的下一个轮询周期停止采集。

### 停止服务

```bash
msmonitor stop
```

`stop` 先回收 processor，再向 collector 下发停止配置后回收 collector，并清理 `~/.msmonitor` 下的运行时状态文件。需要确定性停止采集时，建议先执行 `msmonitor collect stop` 再执行 `msmonitor stop`。

## 输出结果文件说明

`collect start` 下发的配置生效后，训练进程开始采集并落盘。在用户设置的 `--output-dir` 下，即可找到采集到的性能数据文件：

* Jsonl 格式：每行数据包含一条采集到的性能数据，方便用户后续分析；单个文件的最大行数与保留份数由 `--json-rotate-log-lines`、`--json-rotate-log-files` 控制。

processor 读取结果目录第一层的 `msmonitor_<pid>_<timestamp>_<rank>.jsonl` 文件。每个 rank 用自己的设备侧 MSTX range 划分 step，range 名称为 `step N`，domain 为 `default`，N 为数字。训练框架已有这种打点时可直接使用。缺少这一打点时，原始 JSONL 可以采集，逐 step 的指标和 trace 无法生成。

## 附录

### 日志说明

collector 与 processor 各自输出一份日志，默认写入 `<output-dir>/log`。每次启动新建一个文件，每类日志只保留最近 5 份，避免无限堆积：

```text
<output-dir>/log/msmonitor_collector.<host>.<user>.log.<timestamp>
<output-dir>/log/msmonitor_processor.<host>.<user>.log.<timestamp>
```

日志目录默认随 `--output-dir` 一起确定；如需调整位置，可设置 `MSMONITOR_LOG_PATH` 覆盖该默认目录。

`~/.msmonitor` 下的运行时状态文件由 CLI 自动管理，无需手工修改：

| 文件 | 说明 |
| --- | --- |
| `.ready` | collector 的 pid，collector 退出时删除。 |
| `.socket` | collector 监听的 socket 路径。 |
| `.config.json` | `msmonitor start` 的进程级参数，供 `collect start` 复用。 |
| `.processor.pid` | processor 的 pid。 |

### 退出码

命令执行结束后通过进程退出码表示执行结果，可在执行后紧跟 `echo $?` 查看。退出码为 `0` 表示成功，非 `0` 表示失败，含义见下表。以缺少 `--output-dir` 为例，终端会打印如下内容并返回退出码 `2`：

```bash
$ msmonitor start
config error: missing required argument: --output-dir
$ echo $?
2
```

| 退出码 | 说明 |
| --- | --- |
| 0 | 执行成功，包括“未运行时执行停止”这类幂等场景。 |
| 1 | 被 `Ctrl-C` 中断。 |
| 2 | 参数、配置或后端交互错误，如 `--output-dir` 缺失、collector 拉起失败、配置下发失败；终端会输出 `config error: <原因>`。 |
