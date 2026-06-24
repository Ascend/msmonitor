# FAQ

## 一、基本使用

- **Q：dyno CLI 发送 npu-monitor 命令后没有数据上报？**
- A：请按以下顺序排查：

  1. **CANN 版本与 msPTI 动态库检查**：npu-monitor 基于 msPTI 接口实现。
     - CANN ≥ 9.0.0 时，msPTI 自动加载，无需额外设置 `LD_PRELOAD`。
     - CANN < 9.0.0 时，需在启动训练前设置 `export LD_PRELOAD=<CANN-path>/cann/lib64/libmspti.so`。
  2. **dynolog daemon 状态检查**：确认 dynolog daemon 已正常启动，`~`（当前用户主目录）下存在 `dynolog.sock` 文件。
  3. **MSMONITOR_USE_DAEMON 检查**：确认训练或推理服务启动前已设置 `export MSMONITOR_USE_DAEMON=1`。
  4. **共享内存残留检查**：参见本 FAQ "故障排查" 部分关于共享内存残留的处理。
  5. **日志检查**：查看 dynolog 日志中是否正常接收到了 dyno CLI 的 RPC 请求。

- **Q：dyno CLI 发送采集命令后，无法采集到性能数据？**
- A：常见原因及解决方法如下：

  1. **dynolog daemon 未正常启动**
     - 检查 `~`（当前用户主目录）目录下是否存在 `dynolog.sock` 文件，如不存在请先启动 dynolog daemon。

  2. **未设置 MSMONITOR_USE_DAEMON 环境变量**
     - 启动训练或推理服务前，执行 `export MSMONITOR_USE_DAEMON=1`。

  3. **遗留的共享内存文件未清理**
     - msMonitor 底层依赖昇腾 [PyTorch 动态 Profiling](https://gitcode.com/Ascend/pytorch/blob/v2.7.1/docs/zh/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md#%E9%87%87%E9%9B%86%E5%B9%B6%E8%A7%A3%E6%9E%90%E6%80%A7%E8%83%BD%E6%95%B0%E6%8D%AEDynamic_profile) 机制实现能力。Python 3.8+ 环境中，会在 `/dev/shm` 目录生成命名格式为 `DynamicProfileNpuShm+时间戳` 的二进制共享内存文件。正常退出时自动清理；若通过 `pkill -9` 强制终止进程，残留文件会导致短时间（<1h）内采集异常。建议启动前检查 `/dev/shm` 目录，若存在历史残留文件，请手动删除。

  4. **npu-monitor filter 参数设置后无法采集到数据**
     - 检查 `--filter` 参数是否正确，可先去掉 filter 参数验证全量采集是否正常。

  5. **npu-monitor mspti_activity_kind 配置不当**
     - 确认 `--mspti-activity-kind` 参数为允许值之一：`Marker, Kernel, API, Hccl, Memory, MemSet, MemCpy, Communication, AclAPI, NodeAPI, RuntimeAPI`。

  6. **PyTorch 优化器不兼容**
     - 训练场景必须使用 PyTorch 原生优化器或其继承类。自定义优化器需在训练迭代末尾手动调用 `torch_npu.profiler.dynamic_profile.step()`。

- **Q：npu-monitor 和 nputrace 能否同时使用？**
- A：不能。npu-monitor 和 nputrace 共享底层 Profiling 资源，存在资源冲突，必须停止一个后再启动另一个。

- **Q：msMonitor 是否支持 vLLM 推理场景？**
- A：支持，但有限制：
  - vLLM 0.11.0+ 版本，msMonitor 会自动在模型的 forward 方法中调用 `torch_npu.profiler.dynamic_profile.step()`。
  - 旧版本 vLLM，需用户手动调用 `torch_npu.profiler.dynamic_profile.step()`。
  - vLLM 推理模型以 daemon 守护进程方式运行，`nputrace`场景不支持在线解析，采集完成后需手动调用 `torch_npu.profiler.profiler.analyse()` 进行解析。

## 二、参数配置

- **Q：npu-monitor `--duration` 参数如何使用？duration=0 是什么意思？**
- A：`--duration` 参数单位为秒，支持浮点数精度（如 `--duration 0.5` 表示采集 500ms）。
  - `--duration 0`（默认值）：表示不限时采集，需通过 `--npu-monitor-stop` 手动停止。
  - `--duration N`（N>0）：采集 N 秒后自动停止并清理资源。
  - `--duration` 支持与 `--stop` 配合使用：启动时设置较长 duration，在发现异常时通过 stop 提前终止。
  - duration 不与 filter 耦合，可叠加使用，如 `--duration 60 --filter "Kernel:Attention"`。

- **Q：npu-monitor `--duration` 到期后如何重新开始采集？**
- A：duration 到期后，MsptiMonitor 自动停止并释放所有资源。如需继续采集，需重新发送 `--npu-monitor-start` 命令开始新采集会话。

- **Q：npu-monitor `--filter` 参数的格式是什么？支持哪些匹配规则？**
- A：filter 格式为 `"Kind:OpName,OpName;Kind:OpName"`，具体规则如下：
  - 活动类型（Kind）与算子名列表之间用 `:` 分隔。
  - 多个活动类型之间用 `;` 分隔。
  - 同一类型下的多个算子名之间用 `,` 分隔。
  - 算子名支持子串模糊匹配（基于 `string::find`），如 `Mat` 可匹配 `MatMul`、`BatchMatMul`、`MatMulBackward` 等。
  - filter 字符串最大长度 1024 字符。

  **示例**：
  - 仅采集 Kernel 类型中的 MatMul 算子：`--filter "Kernel:MatMul"`
  - 采集多种活动类型：`--filter "Kernel:MatMul,Conv2D;Communication:AllReduce"`
  - 模糊匹配：`--filter "Kernel:Mat"`（匹配所有名称含 "Mat" 的算子）

- **Q：npu-monitor `--filter` 设置后能否动态更新？是覆盖还是追加？**
- A：支持动态更新，且为**覆盖**行为。运行时再次下发包含 filter 的配置，`SetFilterItems()` 会替换原有过滤规则。如需追加算子名，需在 filter 字符串中同时包含新旧规则。

- **Q：npu-monitor Memory/MemSet/MemCpy 类型的记录不受 filter 限制？**
- A：是的。这三种内存操作类型在 `ShouldKeepRecord()` 中被加入白名单，无论 filter 如何设置都会保留，确保内存和性能分析数据的完整性。

- **Q：npu-monitor `--filter ""`（空字符串）的效果是什么？**
- A：空 filter 表示不过滤，所有算子数据均会采集。与不指定 `--filter` 参数行为一致。

- **Q：nputrace `--async-mode` 参数必须在什么条件下生效？**
- A：`--async-mode` 仅在使用 PyTorch Profiler 时生效，MindSpore 场景不支持异步解析。开启后，数据解析在独立子进程中执行，主进程不阻塞。

## 三、故障排查

- **Q：IPC 通信失败如何排查？**
- A：msMonitor 的 IPC 通信链路为：CLI → TCP/TLS（RPC）→ Dynolog Server → Unix Domain Socket → 训练进程。请按以下顺序排查：
  1. 确认 dynolog daemon 已启动，且 `~` 目录下存在 `dynolog.sock`。
  2. 确认 CLI 与 Server 间的 RPC 端口（默认 1778）未被防火墙拦截。
  3. 检查 dynolog 日志中是否有 `ECONNREFUSED`、`EAGAIN` 等 socket 错误码。
  4. IPC 客户端内置指数退避重试机制（最多 10 次），瞬时故障会自动恢复。

- **Q：日志中出现 "Buffer allocation exhausted" 或 "allocCnt" 告警？**
- A：msPTI Buffer 最大并发数默认 32 个（共 256MB），当数据生产速度超过消费速度时触发该限流机制。Buffer 不足时 msPTI 会自动降速丢弃数据，不会导致进程崩溃。可通过以下方式缓解：
  - 缩短 `--report-interval-s`（flush 周期，默认 60s），加速 Buffer 释放。
  - 使用 filter 减少消费数据量，提升 Buffer 消费速率。

- **Q：共享内存文件残留导致采集异常如何处理？**
- A：当训练进程被 `pkill -9` 强制终止时，`/dev/shm` 目录下的 `DynamicProfileNpuShm*` 共享内存文件不会被自动清理。处理方法：

  ```bash
  # 检查残留文件
  ls -la /dev/shm/DynamicProfileNpuShm*
  # 手动删除
  rm -f /dev/shm/DynamicProfileNpuShm*
  ```

  建议在启动训练前将清理命令写入启动脚本。

- **Q：更新 CLI 后服务端不识别新参数？**
- A：如果更新了 `dyno` CLI 但未同步更新 `dynolog` Server，新 CLI 下发的未知命令或参数（如`status`）会被旧 Server 忽略，新参数不会生效且不产生告警。建议保持 CLI（dyno）与 Server（dynolog）版本一致。

- **Q：采集到的 DB/Jsonl 文件为空或数据不完整？**
- A：请检查以下可能性：
  1. **duration 过短**：采集时长不足以触发一次 flush（默认 60s）。建议设置 `--duration` 大于 flush 间隔。
  2. **filter 设置过于严格**：所有记录均被过滤。可先使用空 filter 验证。
  3. **磁盘空间满**：SQLite 写入失败时事务自动回滚。检查磁盘空间并清理。

- **Q：如何确认 msMonitor 当前的工作状态？**
- A：可通过以下方式确认：
  1. 使用 `dyno status` 命令查看 dynolog daemon 运行状态。
  2. 查看 `~`（当前用户主目录）下 `dynolog.sock` 和 `dyno.sock` 文件是否存在。
  3. 检查 dynolog 日志中是否有模型进程注册成功的信息，如 `Registered process (12345) for job 0`。
  4. 通过 `ps`/`top` 查看是否存在 `MsptiMonitor` 命名线程。

## 四、兼容性

- **Q：哪些昇腾产品支持 msMonitor？**
- A：msMonitor 支持以下产品类型，昇腾产品的具体型号，请参见《[昇腾产品形态说明](https://www.hiascend.com/document/detail/zh/AscendFAQ/ProduTech/productform/hardwaredesc_0001.html)》

  | 产品类型 | 是否支持 |
  |---------|:--------:|
  | Atlas 350 加速卡 | √ |
  | Atlas A3 训练系列产品 / Atlas A3 推理系列产品 | √ |
  | Atlas A2 训练系列产品 / Atlas A2 推理系列产品 | √ |
  | Atlas 200I/500 A2 推理产品 | √ |
  | Atlas 推理系列产品 | × |
  | Atlas 训练系列产品 | × |

- **Q：msMonitor 对操作系统和编程语言有什么要求？**
- A：仅支持 **Linux** 操作系统（aarch64 和 x86_64），编程语言使用 C++ / Python。

- **Q：如何更新已安装的 msMonitor 软件包？**
- A：msMonitor 由 dynolog 和 mindstudio_monitor 两个软件包组成，可在 [msMonitor Releases](https://gitcode.com/Ascend/msmonitor/releases) 下载。更新前建议先**查看版本兼容性表**确认各组件版本匹配关系。

  1. **更新 dynolog 软件包**

     ```bash
     # Debian/Ubuntu:
     sudo dpkg -r dynolog
     sudo dpkg -i dynolog_{version}_{arch}.deb --ignore-depends

     # CentOS/RHEL/OpenSUSE:
     sudo rpm -e dynolog
     sudo rpm -ivh dynolog_{version}_{arch}.rpm --nodeps
     ```

     > `version` 为版本号，`arch` 为系统架构（x86_64 / aarch64）。

  2. **更新 mindstudio_monitor 软件包**

     ```bash
     pip uninstall mindstudio_monitor
     pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{arch}.whl
     ```

     > `mindstudio_version` 为 MindStudio 版本号，`python_version` 为 Python 版本号（如 cp310、cp311）。

- **Q：mindstudio_monitor whl 有哪些运行时依赖？**
- A：`mindstudio_monitor` whl 包的运行时依赖如下：

  | 依赖 | 用途 |
  |------|------|
  | pybind11 | Python/C++ 扩展绑定 |
  | xlsxwriter | 将采集的性能数据导出为 Excel 文件（`monitor.save("xxx.xlsx")` 功能） |

  安装 whl 包时，pip 会自动安装以上依赖：

  ```bash
  pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{arch}.whl
  ```

  在不支持联网的环境（如容器），请确保以上依赖已安装，否则会导致 msMonitor 无法正常运行。

- **Q：升级到新版本后，原有采集脚本是否需要修改？**
- A：不需要。`--duration`、`--filter`、`--async-mode` 三个新增参数均为可选参数，有合理的默认值：
  - `duration` 默认 0.0（不限时，行为同旧版本）
  - `filter` 默认空字符串（不过滤，行为同旧版本）
  - `async-mode` 默认 false（同步模式，行为同旧版本）
  旧脚本不指定新参数时，行为与升级前完全一致。

----------------------------------------------------------------------------------------------------------------------------------------------------
