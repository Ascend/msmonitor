# MSMonitor Grafana 看板（beta）

单容器交付包含中心、Grafana 和摘要服务。当 `center.json` 只有一个租户时，Grafana 启动后自动查询该租户的数据。配置多个租户时，在启动命令中加 `-e MSMONITOR_GRAFANA_TENANT=10`，将 `10` 换成要展示的租户 ID。两个查询链路自动使用选中租户的读权限。

## 构建与启动

使用预构建镜像时跳过构建。发布方在仓库根目录执行：

```bash
bash deploy/center/build_image.sh msmonitor-center:beta
```

网络较慢时，可以分别指定 Debian 软件源和 PyPI 源。可选的 `PIP_FALLBACK_INDEX_URL` 在首选源无法解析或下载依赖时，从指定的备用源重新解析整套依赖。已有完整 `deploy/center/control/wheelhouse/` 时，构建直接离线安装其中的 wheel。

```bash
DEBIAN_MIRROR_URL=https://mirrors.huaweicloud.com \
PIP_INDEX_URL=https://mirrors.huaweicloud.com/repository/pypi/simple/ \
PIP_FALLBACK_INDEX_URL=https://pypi.org/simple \
  bash deploy/center/build_image.sh msmonitor-center:beta
```

先按[中心部署指南](../../../docs/zh/install_guide/msmonitor_center.md)准备 `center.json` 和 `tls.pem`。
以下示例使用默认配置目录和租户 10，在新安装时生成 Grafana 管理员初始密码。

```bash
STATE="$HOME/msmonitor-center"
( set -C; openssl rand -hex 24 > "$STATE/config/grafana-admin-password" ) &&
chmod 0644 "$STATE/config/grafana-admin-password" &&
docker run -d --name msmonitor-center-grafana \
  -p 8443:8443 -p 127.0.0.1:3000:3000 \
  --mount "type=bind,src=$STATE/config,dst=/etc/msmonitor,readonly" \
  --mount type=volume,src=msmonitor-center-grafana-data,dst=/data \
  msmonitor-center:beta
```

通过 SSH 转发访问服务器上的 Grafana。

```bash
ssh -N -L 3000:127.0.0.1:3000 用户名@服务器IP
```

打开 [L1 step 耗时分析](http://localhost:3000/d/msmonitor-l1)，使用账号 `admin` 与生成的初始密码登录。训练节点继续向中心 HTTPS 端口上报，页面选择实际训练作业和时间范围。重建容器时复用数据卷以保留时序数据与 Grafana 状态。

### 宿主机端口已占用

如果 `docker run` 报 `port is already allocated` 或 `address already in use`，在上面的启动命令中更换报错的**宿主机端口**。端口映射最右侧的容器内端口保持原值。

| 报错端口 | 启动命令中的修改示例 | 同步修改 |
| --- | --- | --- |
| 中心 HTTPS `8443` | 将 `-p 8443:8443` 改为 `-p 18443:8443` | 训练节点使用的上报和控制地址改用 `18443`。如果 `center.json` 配置了 `public_write_origin`，也改用 `18443`。 |
| Grafana `3000` | 将 `-p 127.0.0.1:3000:3000` 改为 `-p 127.0.0.1:13000:3000` | SSH 转发改为 `ssh -N -L 3000:127.0.0.1:13000 用户名@服务器IP`。浏览器仍访问 `http://localhost:3000`。 |

首次启动失败后，若留下本次创建的 `msmonitor-center-grafana` 容器，先用 `docker inspect --format '{{.State.Status}}' msmonitor-center-grafana` 确认状态为 `created`，再执行 `docker rm msmonitor-center-grafana`。重试时只需重新执行修改后的 `docker run`，沿用原管理员密码文件和数据卷即可。

### 看板故障与健康检查

启用看板时，启动器会校验 Grafana 配置，并等待 Grafana 和看板摘要服务首次就绪。配置错误、进程提前退出或等待超时会使容器启动失败。首次就绪后，退出的看板进程会独立重启，中心仍可接收上报。连续失败时重试间隔从 5 秒逐步增加到 60 秒，并记录失败次数。

Docker 健康检查监控中心服务。可用以下命令单独检查两个看板组件。输出包含各组件的健康状态，任一组件不可用时返回退出码 1。

```bash
docker exec msmonitor-center-grafana python /app/center.py visualization-healthcheck
```

## 生产指标接入

Step 对齐使用同一次运行、同一 Rank 和相同的 TSDB 样本时间戳。L3 使用 `msmonitor_operator_step_id` 验证算子对应的 Step。并行域成员快照通过 `job_name` 和 `run_id` 关联到运行。通算掩盖先在同一 Rank 内平均有效设备样本，再对 Rank 等权平均。

需要显示 L1 缺报和实时上报状态时，可以配置以下环境变量。指标均带 `job_name` 和 `run_id`。

| 环境变量 | 指标口径 |
| --- | --- |
| `MSMONITOR_EXPECTED_RANKS_METRIC` | 每步应报 Rank 数。通过 `step` 标签指定 Step，或以与 `rank="all"` 的 `msmonitor_step_index` 相同的样本时间戳关联。 |
| `MSMONITOR_RECEIVED_TIMESTAMP_METRIC` | 接收端收到 Step 耗时的 Unix 秒时间戳。具体 Rank 的记录与其 Step 样本同时间戳，运行级记录可以带 `step` 标签。 |
| `MSMONITOR_RUN_STATUS_METRIC` | 每次运行的状态 gauge，`0` 已结束、`1` 运行中、`2` 失败。 |
| `MSMONITOR_REPORT_WAIT_SECONDS` | 从该步最早接收时间开始计算的等待秒数。 |

单卡通信图使用同一步的 Stream 级通信算子耗时。选择具体通信算子类型后，按该类型重新计算各 Rank 的累计耗时和实报 Rank 的中位数。切换 Top N 只改变展示的条数。

运行统计逻辑的回归测试。

```bash
python3 -B -m unittest discover -s deploy/center/grafana/backend -p 'test_*.py'
```
