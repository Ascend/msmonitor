# MSMonitor Grafana 看板

在仓库根目录启动 Grafana 和 Prometheus。

```bash
docker compose up -d --build
```

Grafana 默认监听服务器的 `127.0.0.1:3000`。在 Windows PowerShell 建立 SSH 转发并保持窗口打开。

```powershell
ssh -N -L 3000:127.0.0.1:3000 用户名@服务器IP
```

打开 [L1 step 耗时分析](http://localhost:3000/d/msmonitor-l1)。默认登录为 `admin / admin`，也可在首次启动前配置 `GRAFANA_ADMIN_USER` 和 `GRAFANA_ADMIN_PASSWORD`。L1、L2、L3 的顶部链接会传递当前作业、运行和时间范围。直连访问可将 `GRAFANA_HTTP_ADDR` 设置为服务器可以从 Windows 访问的地址。

## 快照验收

- 默认时间范围覆盖 `prometheus-data/` 中带运行标签的采集。默认运行 `capture-20261116-kde-v2` 包含 Step 11–57。
- L1 可点击平均耗时图中的 Step，再打开该步的算子分析。单 Rank 面板可输入起止 Step，也可查看全部保留 Step。
- L2 上方显示用户自定义打点，下方显示通算掩盖。在通算掩盖面板选择 `tp [0,1]`，输入 Rank `1,2` 和 Step `12,14`，悬停应显示两个 Step 均只有一个 Rank。打点按匹配的耗时和调用次数计算，支持在 Host、Device 和全部来源间切换。
- L3 默认选中 Step 12。「单卡通信算子累计耗时」显示实报 Rank 数、中位数、每卡累计耗时和偏移。计算与通信 KDE 各有面板，图下方列出所有 Stream 的簇明细。查询 Rank 0 时，计算侧 `FlashAttentionScore` 的 Stream 47 有 112 次调用，通信侧 `hcom_allReduce_` 的 Stream 40 有 904 次调用。

## 生产指标接入

Step 对齐使用同一次运行、同一 Rank 和相同的 TSDB 样本时间戳。L3 使用 `msmonitor_operator_step_id` 验证算子对应的 Step。并行域成员快照通过 `job_name` 和 `run_id` 关联到运行。通算掩盖先在同一 Rank 内平均有效设备样本，再对 Rank 等权平均。

快照未提供 L1 缺报和实时上报状态所需的全部指标。已有对应指标时，可以配置以下环境变量。指标均带 `job_name` 和 `run_id`。

| 环境变量 | 指标口径 |
| --- | --- |
| `MSMONITOR_EXPECTED_RANKS_METRIC` | 每步应报 Rank 数。通过 `step` 标签指定 Step，或以与 `rank="all"` 的 `msmonitor_step_index` 相同的样本时间戳关联。 |
| `MSMONITOR_RECEIVED_TIMESTAMP_METRIC` | 接收端收到 Step 耗时的 Unix 秒时间戳。具体 Rank 的记录与其 Step 样本同时间戳，运行级记录可以带 `step` 标签。 |
| `MSMONITOR_RUN_STATUS_METRIC` | 每次运行的状态 gauge，`0` 已结束、`1` 运行中、`2` 失败。 |
| `MSMONITOR_REPORT_WAIT_SECONDS` | 从该步最早接收时间开始计算的等待秒数。 |

单卡通信图使用同一步的 Stream 级通信算子耗时。选择具体通信算子类型后，按该类型重新计算各 Rank 的累计耗时和实报 Rank 的中位数。切换 Top N 只改变展示的条数。

运行统计逻辑的回归测试。

```bash
python3 -B -m unittest discover -s grafana/backend -p 'test_*.py'
```
