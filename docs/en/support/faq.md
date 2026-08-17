# FAQs

<!-- md-trans-meta sourceCommit=4222e88544e08580745f0b6eec2d1d22cda8f31b translatedAt=2026-08-12T08:00:27.870Z pushedAt=2026-08-12T08:06:11.588Z -->

## 1. Basic Usage

- **Q: Why does the npu-monitor command via the dyno CLI report no data?**

- A: Troubleshoot in the following order:

  1. **CANN version and msPTI dynamic library check**: npu-monitor is implemented based on the msPTI interface.

     - When CANN ≥ 9.0.0, msPTI is automatically loaded, and no additional `LD_PRELOAD` setting is required.

     - For CANN < 9.0.0, set `export LD_PRELOAD=<CANN-path>/cann/lib64/libmspti.so` before starting training.

  2. **dynolog daemon status check**: Confirm that the dynolog daemon has started normally and that the `dynolog.sock` file exists under `~` (the current user home directory).

  3. **MSMONITOR_USE_DAEMON check**: Confirm that `export MSMONITOR_USE_DAEMON=1` has been set before starting the training or inference service.

  4. **Shared Memory residue check**: See the "Troubleshooting" section of this FAQ for handling Shared Memory residue.

  5. **Log check**: Check whether the dynolog log has normally received RPC requests from the dyno CLI.

- **Q: Why can't I collect profile data after sending a collection command via the dyno CLI?**

  - A: Common causes and solutions are as follows:

    1. **dynolog daemon not started properly**

        - Check whether the `dynolog.sock` file exists in the `~` (the current user home directory) directory. If not, start the dynolog daemon first.

    2. **`MSMONITOR_USE_DAEMON` environment variable not set**

        - Before starting the training or inference service, run `export MSMONITOR_USE_DAEMON=1`.

    3. **Residual shared memory files not cleaned up**

        - msMonitor relies on the Ascend [PyTorch Dynamic Profiling](https://gitcode.com/Ascend/pytorch/blob/v2.7.1-26.1.0/docs/en/ascend_pytorch_profiler/ascend_pytorch_profiler_user_guide.md#%E9%87%87%E9%9B%86%E5%B9%B6%E8%A7%A3%E6%9E%90%E6%80%A7%E8%83%BD%E6%95%B0%E6%8D%AEDynamic_profile) mechanism for its underlying capabilities. In Python 3.8+ environments, binary shared memory files named in the format `DynamicProfileNpuShm+timestamp` are generated in the `/dev/shm` directory. They are automatically cleaned up upon normal exit. However, if the process is forcibly terminated using `pkill -9`, residual files may cause collection anomalies within a short period (< 1 h). It is recommended to check the `/dev/shm` directory before startup and manually delete any historical residual files if present.

    4. **No data collected after setting the npu-monitor filter parameter**

        - Check whether `--filter` is correct. You can first remove the `filter` parameter to verify whether full collection works properly.

    5. **Improper configuration of npu-monitor mspti_activity_kind**

        - Verify that `--mspti-activity-kind` is one of the allowed values: `Marker, Kernel, API, Hccl, Memory, MemSet, MemCpy, Communication, AclAPI, NodeAPI, RuntimeAPI`.
     
    6. **PyTorch Optimizer incompatibility**

      - In training scenarios, the PyTorch native optimizer or its derived classes must be used. For custom optimizers, `torch_npu.profiler.dynamic_profile.step()` must be manually called at the end of each training iteration.

- **Q: Can npu-monitor and nputrace be used simultaneously?**

- A: No. npu-monitor and nputrace share underlying Profiling resources, which causes resource conflicts. One must be stopped before the other is started.

- **Q: Does msMonitor support vLLM inference scenarios?**

- A: Yes, but with limitations:

  - For vLLM 0.11.0 and later versions, msMonitor automatically calls `torch_npu.profiler.dynamic_profile.step()` in the model's forward method.

  - For earlier versions of vLLM, users must manually call `torch_npu.profiler.dynamic_profile.step()`.

- The vLLM inference model runs as a daemon. The `nputrace` scenario does not support online analysis. After collection is complete, `torch_npu.profiler.profiler.analyse()` must be manually invoked for analysis.

## II. Parameter Configuration

- **Q: How is npu-monitor `--duration`  used? What does `duration=0` mean?**

- A: The unit of `--duration` is seconds, and floating-point precision is supported (for example, `--duration 0.5` indicates collection for 500 ms).

  - `--duration 0` (default): Indicates unlimited collection, which must be stopped manually via `--npu-monitor-stop`.

  - `--duration N` (N > 0): Automatically stops and cleans up resources after N seconds of collection.

  - `--duration` used together with `--stop`: Sets a longer duration at startup, and terminates early via `stop` when an anomaly is detected.

  - `duration` is not coupled with `filter` and can be used in combination, for example, `--duration 60 --filter "Kernel:Attention"`.

- **Q: How Do I restart collection after the npu-monitor `--duration` expires?**

- A: After the duration expires, MsptiMonitor automatically stops and releases all resources. To continue collection, the `--npu-monitor-start` command must be re-sent to begin a new collection session.

- **Q: What is the format of npu-monitor `--filter`? What matching rules are supported?**

- A: The filter format is `"Kind:OpName,OpName;Kind:OpName"`, with the following specific rules:

  - Activity kind and the operator name list are separated by `:`.

  - Multiple activity types are separated by `;`.

  - Multiple operator names under the same type are separated by `,`.

  - Operator names support substring fuzzy matching (based on `string::find`). For example, `Mat` can match `MatMul`, `BatchMatMul`, `MatMulBackward`, and so on.

  - The maximum length of the filter string is 1024 characters.

  **Examples**:

  - Collect only the MatMul operator under the Kernel type: `--filter "Kernel:MatMul"`

  - Collect multiple activity Kinds: `--filter "Kernel:MatMul,Conv2D;Communication:AllReduce"`

  - Fuzzy matching: `--filter "Kernel:Mat"` (matches all operators whose names contain "Mat")

- **Q: After npu-monitor `--filter` is set, can it be dynamically updated? Is it an overwrite or an append?**

- A: Dynamic update is supported, and it is an **overwrite** behavior. When a configuration containing a `filter` is issued again at runtime, `SetFilterItems()` replaces the original filtering rules. If additional operator names need to be appended, both the old and new rules must be included in the filter string.

- **Q: Are npu-monitor Memory/MemSet/MemCpy type records not subject to filter restrictions?**

- A: Yes. These three memory operation types are added to the whitelist in `ShouldKeepRecord()`, and they are retained regardless of the filter setting, ensuring the integrity of memory and performance analysis data.

- **Q: What is the effect of npu-monitor `--filter ""` (empty string)?**

- A: An empty filter means no filtering is applied, and all operator data will be collected. This behavior is the same as when `--filter` is not specified.

- **Q: Under what conditions must nputrace `--async-mode` take effect?**

- A: `--async-mode` takes effect only when PyTorch Profiler is used. Asynchronous parsing is not supported in MindSpore scenarios. When the mode is enabled, data parsing is executed in an independent subprocess, and the main process is not blocked.

## III. Troubleshooting

- **Q: How do I troubleshoot IPC communication failures?**

- A: The IPC communication chain of msMonitor is: CLI → TCP/TLS (RPC) → Dynolog Server → Unix Domain Socket → training process. Troubleshoot in the following order:

  1. Confirm that the dynolog daemon is started and that `dynolog.sock` exists in the `~` directory.

  2. Confirm that the RPC port (default `1778`) between the CLI and the Server is not blocked by the firewall.

  3. Check the dynolog log for socket error codes such as `ECONNREFUSED` and `EAGAIN`.

  4. The IPC client has a built-in exponential backoff retry mechanism (up to 10 times), and transient faults are automatically recovered.

- **Q: How do I troubleshoot "Buffer allocation exhausted" or "allocCnt" alarms appear in the log?**

- A: The default maximum concurrency of msPTI buffers is 32 (256 MB in total). This rate limiting mechanism is triggered when the data production rate exceeds the consumption rate. When the buffer is insufficient, msPTI automatically slows down and drops data without causing process crashes. This can be mitigated through the following methods:

  - Shorten `--report-interval-s` (flush interval, default 60s) to accelerate buffer release.

  - Use filters to reduce the amount of consumed data and increase the buffer consumption rate.

- **Q: How do I troubleshoot collection exceptions caused by residual shared memory files?**

- A: When a training process is forcibly terminated by `pkill -9`, the `DynamicProfileNpuShm*` shared memory files under the `/dev/shm` directory are not automatically cleaned up. Handling method:

  ```bash
  # Check for residual file
  ls -la /dev/shm/DynamicProfileNpuShm*
  # Manually delete it
  rm -f /dev/shm/DynamicProfileNpuShm*
  ```

It is recommended to write the cleanup command into the startup script before launching training.

- **Q: What should I do if the server does not recognize new parameters after updating the CLI?**

- A: If the `dyno` CLI is updated but the `dynolog` Server is not synchronously updated, unknown commands or parameters (such as `status`) sent by the new CLI will be ignored by the old Server. The new parameters will not take effect and no alarm will be generated. It is recommended to keep the CLI (`dyno`) and Server (`dynolog`) versions consistent.

- **Q: What should I do if the collected DB/Jsonl file is empty or contains incomplete data?****

- A: Check the following possibilities:

  1. **Duration too short**: The collection duration is insufficient to trigger a flush (default 60s). It is recommended to set `--duration` to be greater than the flush interval.

  2. **Filter settings too strict**: All records are filtered out. An empty filter can be used first for verification.

  3. **Disk full**: When SQLite write fails, the transaction is automatically rolled back. Check the disk space and clean up.

- **Q: How can I verify the current working status of msMonitor?**

- A: The status can be confirmed through the following methods:

  1. Use the `dyno status` command to check the running status of the dynolog daemon.

  2. Check whether the `dynolog.sock` and `dyno.sock` files exist under `~` (the current user home directory).

  3. Check the dynolog logs for information indicating successful model process registration, such as `Registered process (12345) for job 0`.

  4. Use `ps`/`top` to check whether a thread named `MsptiMonitor` exists.

## IV. Compatibility

- **Q: Which Ascend products are supported by msMonitor?**

- A: msMonitor supports the following product types. For specific Ascend product models, see *[Ascend Product Form Description](https://www.hiascend.com/document/detail/en/AscendFAQ/ProduTech/productform/hardwaredesc_0001.html)*.

  | Product Type | Supported |
  |---------|:--------:|
  | Atlas 350 Accelerator Card | √ |
  | Atlas A3 Training Series/Atlas A3 Inference Series | √ |
  | Atlas A2 Training Series/Atlas A2 Inference Series | √ |
  | Atlas 200I/500 A2 Inference Product | √ |
  | Atlas Inference Series | × |
  | Atlas Training Series | × |

- **Q: What are the operating system and programming language requirements for msMonitor?**

- A: Only the **Linux** operating system (aarch64 and x86_64) is supported, with C++/Python as the programming languages.

- **Q: How do I update an installed msMonitor software package?**

- A: msMonitor consists of two software packages, dynolog and mindstudio_monitor, which can be downloaded from [msMonitor Releases](https://gitcode.com/Ascend/msmonitor/releases). Before updating, it is recommended to **check the version compatibility table** to confirm that the versions of all components match.

  1. **Update the dynolog software package.**

     ```bash
     # Debian/Ubuntu:
     sudo dpkg -r dynolog
     sudo dpkg -i dynolog_{version}_{arch}.deb --ignore-depends

     # CentOS/RHEL/OpenSUSE:
     sudo rpm -e dynolog
     sudo rpm -ivh dynolog_{version}_{arch}.rpm --nodeps
     ```

     > `version` is the version number, and `arch` is the system architecture (x86_64 / aarch64).

  2. **Update the mindstudio_monitor software package.**

     ```bash
     pip uninstall mindstudio_monitor
     pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{arch}.whl
     ```

     > `mindstudio_version` is the MindStudio version number, and `python_version` is the Python version number (e.g., cp310, cp311).

- **Q: What are the runtime dependencies of the mindstudio_monitor .whl?**

- A: The runtime dependencies of the `mindstudio_monitor` .whl package are as follows:

  | Dependency | Purpose |
  |------|------|
  | pybind11 | Python/C++ extension binding |
  | xlsxwriter | Export collected profile data to an Excel file (`monitor.save("xxx.xlsx")`) |

  When installing the .whl package, pip automatically installs the above dependencies:

  ```bash
  pip install mindstudio_monitor-{mindstudio_version}-cp{python_version}-cp{python_version}-linux_{arch}.whl
  ```

  In environments without network access (such as containers), ensure that the above dependencies are already installed; otherwise, msMonitor may fail to run properly.

- **Q: After upgrading to the new version, do existing collection scripts need to be modified?**

- A: No.  `--duration`, `--filter`, and `--async-mode` are all optional and have reasonable default values:

  - `duration` defaults to `0.0` (unlimited duration, behavior same as previous versions)

  - `filter` defaults to an empty string (no filtering, behavior same as previous)

  - `async-mode` defaults to `false` (synchronous mode, behavior same as previous)

  When old scripts do not specify new parameters, the behavior is fully consistent with that before the upgrade.
