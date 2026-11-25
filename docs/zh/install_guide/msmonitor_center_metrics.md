# 指标写入与时序查询的 Kubernetes 部署（beta）

本指南用于独立部署指标写入（I3）和时序查询（I4）。HAProxy 在传输层（L4）按 TCP 连接分发写入流量。部署包含节点注册与控制服务（I8）的完整中心时，请使用[中心统一安装器](msmonitor_center.md)。

入口使用平台 HTTPS 网关，后端指向同一命名空间的 `msmonitor-i3-auth:8427`。vmauth 按租户及读写角色鉴权。写入通过 HAProxy 以 TCP 连接为单位轮询 vminsert Pod，查询交给 vmselect。Operator 管理 VMCluster 和 vmstorage 持久卷。[VMCluster 服务与端口](https://docs.victoriametrics.com/operator/resources/vmcluster/#services-and-urls)

模板提供三个跨节点写入副本、两个查询副本和三个存储副本。存储复制因子为 2，查询按毫秒去重。1 天保留期、每存储副本 10Gi 和资源配置用于初始部署验证。连接数、吞吐、资源与 ≥50% 写入故障余量需在目标集群实测，生产配置按结果调整。单机 Docker POC 使用两个写入实例。[VictoriaMetrics 复制与数据安全](https://docs.victoriametrics.com/victoriametrics/cluster-victoriametrics/#replication-and-data-safety)

部署前确认：

- Operator 已安装并监视目标命名空间，`vmclusters.operator.victoriametrics.com` CRD 已就绪。模板字段依据 Operator v0.75.2 校验。
- 有默认 StorageClass，或在 `vmcluster.yaml` 的 PVC 模板中填入平台的 `storageClassName`。确认保留期、PV 大小与备份策略。
- CNI 支持 NetworkPolicy。使用受控的中心命名空间，限制用户创建或修改带中心组件标签的 Pod。把 `gateway.yaml` 最后一条策略中的 `replace-with-gateway-namespace` 改成受控的网关命名空间，需要时再加网关 Pod 标签。实际入口拓扑由平台确认。网关只发布约定的写入和查询路径，并保留请求路径。生产的域名、TLS 证书、网关访问范围、监控来源和镜像仓库由平台设置。
- 默认 DNS 域是 `cluster.local`。自定义时同时修改 `vmcluster.yaml` 的 `clusterDomainName` 和 `gateway.yaml` 的 `CLUSTER_DOMAIN`。模板使用 32 个 HAProxy 后端槽位，扩容前调大 `server-template` 槽位数，覆盖 vminsert 副本数及滚动升级新增的 Pod。
- 写入节点需要至少三个可调度且 `kubernetes.io/hostname` 值不同的节点。目标 Kubernetes 和 VMCluster CRD 须支持 `topologySpreadConstraints.minDomains` 与 `nodeTaintsPolicy`。部署后核对 vminsert Deployment 保留调度约束，并检查 Pod 所在节点。[Kubernetes 拓扑分布约束](https://kubernetes.io/docs/concepts/scheduling-eviction/topology-spread-constraints/)

可用 `kubectl -n <网关命名空间> get pods --show-labels` 查看网关 Pod 的稳定标签。在最后一条 NetworkPolicy 的同一条 `from` 规则中给 `namespaceSelector` 配上 `podSelector`，同时限制命名空间和网关 Pod。

```yaml
- namespaceSelector:
    matchLabels:
      kubernetes.io/metadata.name: ingress-system
  podSelector:
    matchLabels:
      app.kubernetes.io/name: ingress-controller
```

在仓库根目录用 Python 3 和 kubectl 预检。脚本检查目标命名空间、CRD、StorageClass、网关命名空间、DNS 域和写入槽位，并在 API Server 上 dry-run 两份清单，不写入资源或读取凭据。

```bash
python3 deploy/center/metrics/preflight.py -n msmonitor-system
```

预检通过后还需确认 Operator 监听该命名空间、CNI 策略实际生效，并在部署后检查镜像拉取、PVC 绑定和 HTTPS 入口。部署前把 `auth.yaml.example` 复制到受控目录，替换租户和各自不同的读写 Token。配置文件保持仅当前用户可读，通过平台的凭据渠道分发。

```bash
(
set -euo pipefail
NS=msmonitor-system
AUTH_CONFIG=/path/to/private/auth.yaml
python3 deploy/center/metrics/preflight.py -n "$NS"
test -s "$AUTH_CONFIG" && ! grep -q 'REPLACE_' "$AUTH_CONFIG" || exit 1

kubectl -n "$NS" create secret generic msmonitor-i3-auth \
  --from-file="auth.yaml=$AUTH_CONFIG" --dry-run=client -o yaml \
  | kubectl -n "$NS" apply --server-side -f -
kubectl -n "$NS" apply \
  -f deploy/center/metrics/vmcluster.yaml -f deploy/center/metrics/gateway.yaml
kubectl -n "$NS" get vmcluster,pod,pvc,svc,networkpolicy
)
```

配置轮转沿用 Secret 更新，vmauth 定期重新读取配置。HAProxy ConfigMap 修改后滚动重启 `deployment/msmonitor-i3-l4`。生产网关只连接 vmauth Service。HAProxy 管理与健康端口、vmauth 内部接口和 VMCluster 组件保留在集群内。[vmauth 路由与鉴权](https://docs.victoriametrics.com/victoriametrics/vmauth/#routing-by-path)

部署后验证：

1. vmstorage 的 PVC 全部 `Bound`，三个 vminsert、两个 vmselect、三个 vmstorage、两个 vmauth 和两个 HAProxy Pod 均就绪。用 `kubectl -n "$NS" get pod -l msmonitor.io/i3-component=insert -o wide` 确认三个写入 Pod 的 `NODE` 各不相同。
2. 在平台 HTTPS 入口使用租户 10 写入 Token 调用 `/insert/10/prometheus/api/v1/write`，使用租户 10 查询 Token 通过 `/select/10/prometheus/api/v1/query` 查回。换租户、换读写权限和省略凭据的请求均被拒绝。
3. 核对写入连接到达各 vminsert。可对某个 HAProxy Pod 做端口转发 `kubectl -n "$NS" port-forward pod/<haproxy-pod> 18404:8404`，从 `http://127.0.0.1:18404/stats;csv` 查看各后端的会话计数。使用多个独立连接，确认各后端均有增量。处置一个 vminsert 副本后继续上报，核对剩余实例接收流量和样本完整性。
4. 从非授权命名空间确认无法直连 vmauth、HAProxy 或 VictoriaMetrics 后端。按平台的监控部署添加必要的抓取来源，再确认内部健康和指标接口可用。

## I5 内部监控接入

HAProxy 的业务监听保持 TCP 模式，后端通过独立的 `GET /-/healthy` 探活。状态端口 8404 提供 `/livez`、`/readyz`、`/stats` 和 `/metrics`。该端口用于内部监控和排障。

复用平台的 VMAgent 或兼容 Prometheus 的采集器。将 `deploy/center/monitoring.yaml.example` 复制到受控配置目录，在部署前填写真实的采集器和节点控制服务 HTTPS 网关命名空间、Pod 标签。采集端访问 VM 组件的业务 HTTP 端口，因此这些标签只授予受信任的平台组件。控制服务的网关来源规则必须匹配实际网关，保持注册和心跳可达。使用 VMAgent 时确认其发现中心命名空间中的 VMPodScrape 和 Operator 为 VMCluster 生成的 VMServiceScrape，并核对同一目标只抓取一次。

```bash
kubectl -n "$NS" apply --dry-run=server --validate=strict -f /path/to/center-monitoring.yaml
kubectl -n "$NS" apply -f /path/to/center-monitoring.yaml
```

在采集端核对各预期副本的 `up` 和实际指标。重点看 HAProxy 健康后端与会话数，VM 写入速率、磁盘、查询延迟，以及控制服务的请求和连接池等待。再从非授权命名空间验证指标与控制端口不可直达，并确认正式网关下 指标写入、时序查询和节点注册/心跳仍正常。

## 目标 K8s 单 Pod 故障演练

使用独立验收命名空间、测试租户和专用测试数据。确认当前没有升级、其他异常或故障演练。三个 vminsert、三个 vmstorage 及查询、鉴权和 TCP 负载均衡副本应全部就绪，存储复制因子为 2，PVC 均为 `Bound`。核对 `NODE` 列，写入和存储各自分散在三个不同节点。本节验证单 Pod 退出和重建，≥50% 写入故障余量按目标写入池和负载单独验证。

```bash
CTX='填验收集群的 kubectl context'
NS='填独立验收命名空间'
k() { kubectl --context "$CTX" -n "$NS" "$@"; }
k get vmcluster msmonitor -o 'jsonpath={.spec.replicationFactor}{"\n"}'
k get pods -l msmonitor.io/i3-component -o wide
k get pvc
k wait --for=condition=Ready pod -l msmonitor.io/i3-component --timeout=120s
```

在另一终端按[持续写入和回查说明](../development_guide/msmonitor_center_test.md)启动写入探针，使用实际 HTTPS 地址、证书及同租户读写凭据。每个故障单独运行一轮。先确认基线样本写入并可回查，再执行下列对应步骤。可用 `--duration 180 --visibility-timeout 30 --max-unseen 0` 起步，按预计恢复时间调整运行时长，覆盖故障前、中、后阶段。`--max-write-failures` 按验收允许的暂时写入失败数填写。

1. **退出一个写入 Pod。** 从 `insert` 标签对应的写入 Pod 中选一个。记录删除前后时间、HAProxy 后端状态及会话计数，确认流量切到剩余写入实例。替代 Pod 就绪且后端恢复后，再进行下一轮。

   ```bash
   k get pods -l msmonitor.io/i3-component=insert -o wide
   WRITER='填一个列出的 vminsert Pod 名'
   (
     set -euo pipefail
     [ "$(k get pod "$WRITER" -o 'jsonpath={.metadata.labels.msmonitor\.io/i3-component}')" = insert ]
     date -Is
     k delete pod "$WRITER" --timeout=60s
     k rollout status deployment/vminsert-msmonitor --timeout=120s
     k get pods -l msmonitor.io/i3-component=insert -o wide
   )
   ```

2. **退出一个存储 Pod。** 从三个 `storage` Pod 中选一个。只删除 Pod，保留 PVC，并核对替代 Pod 的 UID 变化、原 PVC 仍为 `Bound` 且 UID 一致。下列命令用当前模板中挂到 `/vm-data` 的卷定位 PVC。

   ```bash
   k get pods -l msmonitor.io/i3-component=storage -o wide
   STORE='填一个列出的 vmstorage Pod 名'
   (
     set -euo pipefail
     [ "$(k get pod "$STORE" -o 'jsonpath={.metadata.labels.msmonitor\.io/i3-component}')" = storage ]
     OLD_UID=$(k get pod "$STORE" -o 'jsonpath={.metadata.uid}')
     VOL=$(k get pod "$STORE" -o 'jsonpath={.spec.containers[?(@.name=="vmstorage")].volumeMounts[?(@.mountPath=="/vm-data")].name}')
     [ -n "$VOL" ]
     PVC=$(k get pod "$STORE" -o "jsonpath={.spec.volumes[?(@.name==\"$VOL\")].persistentVolumeClaim.claimName}")
     [ -n "$PVC" ]
     PVC_UID=$(k get pvc "$PVC" -o 'jsonpath={.metadata.uid}')
     [ "$(k get pvc "$PVC" -o 'jsonpath={.status.phase}')" = Bound ]
     date -Is
     k delete pod "$STORE" --timeout=60s
     k wait --for=create "pod/$STORE" --timeout=120s
     k wait --for=condition=Ready "pod/$STORE" --timeout=120s
     [ "$(k get pod "$STORE" -o 'jsonpath={.metadata.uid}')" != "$OLD_UID" ]
     [ "$(k get pvc "$PVC" -o 'jsonpath={.metadata.uid}')" = "$PVC_UID" ]
     [ "$(k get pvc "$PVC" -o 'jsonpath={.status.phase}')" = Bound ]
     k get pods -l msmonitor.io/i3-component=storage -o wide
   )
   ```

任一恢复等待超时就暂停后续故障，先用事件和组件日志定位。留存 canary 输出、故障时间、就绪恢复耗时、HAProxy 后端状态、VM 写入错误和存储状态。按样本时间戳与值核对故障前、故障中和恢复后的数据，查询应完整且无 `isPartial: true`，已确认写入的样本应全部可见。网络或写入失败另计，并按 daemon 重试能力及验收阈值判断。1 毫秒去重按逻辑样本计数。这些步骤不模拟 PVC 损坏，也不代替备份恢复演练。[VictoriaMetrics 故障行为与完整查询](https://docs.victoriametrics.com/victoriametrics/cluster-victoriametrics/#cluster-availability)
