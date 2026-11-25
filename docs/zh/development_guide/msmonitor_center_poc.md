# 中心写入 POC（beta）

在一台 Linux Docker 主机上启动 VictoriaMetrics Cluster、vmauth 和 HAProxy。vmauth 对外提供两个测试租户的鉴权写入和查询路由，HAProxy 按 TCP 连接在两个 vminsert 之间轮询分发。存储、查询和写入实例位于独立内部网络。

在 Docker 宿主机运行一次：

```bash
python3 deploy/center/poc/deploy.py
docker ps --filter 'name=req2-final-'
```

入口默认绑定 `127.0.0.1:18467`。租户 10 的写入路径为 `/insert/10/prometheus/api/v1/write`。查询路径为 `/select/10/prometheus/api/v1/` 下的 `query`、`query_range`、`series` 和 `labels`。租户 20 使用对应编号。默认配置目录 `~/req2-final` 中的 `tokens.json` 保留之前的测试读写凭据。使用 `write-tokens.json` 和 `query-tokens.json` 可分别验证写入和查询权限。请保持这些文件的访问权限。

要在保留之前环境的情况下验证新路由和 HTTP 探活，使用独立的容器前缀、新目录和空闲端口。

```bash
POC_WORK="$HOME/req2-center-v2"
python3 deploy/center/poc/deploy.py --prefix req2-center-v2 \
  --work "$POC_WORK" --port 18477 --stats-port 18478
curl -fsS --retry 10 --retry-connrefused http://127.0.0.1:18478/readyz
python3 test/st/center/test_tenant_queries.py --origin http://127.0.0.1:18477 \
  --write-tokens "$POC_WORK/write-tokens.json" --query-tokens "$POC_WORK/query-tokens.json"
```

指定 `--stats-port` 后，HAProxy 的 `/readyz`、`/livez`、`/stats` 和 `/metrics` 仅通过宿主机回环端口提供。此部署用于验证中心链路，完整中心的部署步骤见 [中心部署说明](../install_guide/msmonitor_center.md)。
