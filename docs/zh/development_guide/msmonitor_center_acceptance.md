# 中心独立验收（beta）

在专用测试环境从仓库根目录执行，保存每轮输出、镜像和资源配置。Linux Compose 使用真实 PostgreSQL 和双 I8 副本；单容器使用包含所有中心组件的最终镜像。测试以模拟 daemon 提交任务和摘要，原始 trace 由节点实现。

```bash
bash test/st/center/run_center_lab.sh i8
bash test/st/center/run_center_lab.sh mixed 160
bash test/st/center/run_center_lab.sh standalone
```

第一项在迁移后执行注册、心跳、任务、结构化摘要及独立库恢复回查。第二项同时运行轮询、心跳和任务确认，输出成功数、延迟、迟发请求以及前后两份数据库池指标。按机器预算逐级增加到 1000、5000、12500 个逻辑节点；记录限流、客户端文件描述符和服务资源瓶颈。第三项构建最终镜像并生成临时凭据、证书和专用卷，验证多租户读写、范围查询、元数据查询、注册发现和同卷重建。I5 用独立 Docker fixture 演练 HTTP 探活失败时摘除后端。

## 多机 Kubernetes 与 M5

平台提供专用 context、namespace、Ingress、VictoriaMetrics Operator、存储卷及可切换主备的 PostgreSQL。按 [部署步骤](../install_guide/msmonitor_center.md) 执行服务端预检、部署和 TLS 检查，然后执行 [I3/I5 故障与网络验证](../install_guide/msmonitor_center_metrics.md)。在网关外部用两租户的独立读写凭据执行 test_tenant_queries.py；在无授权 Pod 中检查无法绕过网关直连 vminsert/vmselect/I8。用平台监控核对 VM 分片指标、HAProxy 后端健康/会话和 I8 池指标实际可抓取。凭据轮转、滚动更新期间持续注册、心跳和提交相同批次，核对旧凭据撤销及数据持久化。

准备测试租户的 WRITE、READ 凭据映射文件及 CA，设置 HOST、PORT、TENANT。下列命令会产生大量时序，仅在容量验收集群运行。在独立压测机同时运行两个进程，分别保留退出码；若单机临时端口不足，将连接数分摊至多台机器，同时记录入口连接峰值。查询结果禁止部分响应。

```bash
auth=(--tenant "$TENANT" --tokens "$WRITE" --query-tokens "$READ" --tls --ca "$CA")
python3 test/st/center/test_series_cardinality.py "$HOST" "$PORT" 12000000 "${auth[@]}" --rounds 2
# 在另一终端与上述负载重叠运行；若基数测试超过 600 秒，分轮连续运行并保存每轮结果。
python3 test/st/center/remote_write_load.py "$HOST" "$PORT" 30000 180 8 "${auth[@]}" --verify-readback --readback-timeout 600
```

记录两轮相同 1200 万标签集的接受样本数和分组查询结果、重叠期查询延迟、实际同时连接数及应用层吞吐。复用 remote_write_bandwidth.sh 检验目标 9 MB/s。对三个 vminsert 的模板，在持续 canary 和目标负载期间，用测试命名空间的 VMCluster 控制写入实例数，避免 Operator 立即补回 Pod：

```bash
(
  set -eu
  k() { kubectl --context "$CTX" -n "$NS" "$@"; }
  [ "$(k get vmcluster msmonitor -o jsonpath='{.spec.vminsert.replicaCount}')" = 3 ]
  restore() { k patch vmcluster msmonitor --type=merge -p '{"spec":{"vminsert":{"replicaCount":3}}}'; }
  trap restore EXIT
  k patch vmcluster msmonitor --type=merge -p '{"spec":{"vminsert":{"replicaCount":1}}}'
  k wait --for=jsonpath='{.status.readyReplicas}'=1 deployment/vminsert-msmonitor --timeout=180s
  python3 test/st/center/remote_write_load.py "$HOST" "$PORT" 5100 120 82 "${auth[@]}" --min-steady-body-bytes-per-second 9000000 --verify-readback --readback-timeout 600
)
```

记录实际 Pod 数、HAProxy 健康和返回三副本后的就绪情况。控制面主备验证须使用平台真实 PostgreSQL 故障切换入口，切换前保存 session、已确认批次和命令结果；切换中记录 503 与重试，切换后以原 session 回报心跳、重传原批次并查询命令，确认无第二份数据。专用 Compose 的停库恢复和备份恢复用例分别覆盖服务中断及数据恢复。训练性能、daemon outbox 与真实节点 trace 执行由联调验收。
