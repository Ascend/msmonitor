# 中心侧并行验收（beta）

使用模拟客户端验证 I3 写入、I8 注册和心跳。真实指标字段、节点侧采集和压缩、每 step 或每 N step 发送、摘要窗口以及异常优先上报，在正式 daemon 就绪后联验。

## 中心功能独立验证

在中心 Linux 测试机从当前版本的仓库根目录执行。需要 Python 3、Docker 和 Docker Compose。使用独立目录和回环端口，能与旧 POC 并存。

```bash
I3_STATE="$PWD/req2-center-v2"
python3 deploy/center/poc/deploy.py \
  --prefix req2-center-v2 --work "$I3_STATE" --port 18477 --stats-port 18478 &&
python3 test/st/center/test_tenant_queries.py \
  --origin http://127.0.0.1:18477 \
  --write-tokens "$I3_STATE/write-tokens.json" \
  --query-tokens "$I3_STATE/query-tokens.json" &&
python3 test/st/center/test_writer_health.py --origin http://127.0.0.1:18478
```

确认同租户不同作业的即时及范围聚合、两种元数据查询、跨租户和读写权限拒绝均通过。I5 应报告后端处于健康状态且 Prometheus 指标可采集。另用独立 Docker 夹具验证只关闭 HTTP 健康状态也能摘除后端。夹具自行创建和清理临时容器。

```bash
python3 test/st/center/test_writer_health.py --docker-fixture
```

验证 I8 新旧注册和心跳，在新端口启动两副本与 PostgreSQL。

```bash
export I8_LAB_PROJECT=req2-center-v2-i8
export I8_LAB_STATE="$PWD/i8-lab-state"
export I8_LAB_PORT_A=18571 I8_LAB_PORT_B=18572
export I8_LAB_IMAGE=msmonitor-i8:center-v2-lab
bash deploy/center/control/lab.sh up &&
bash deploy/center/control/lab.sh test --verify-metadata
```

确认旧请求的响应兼容，扩展元数据校验、重复/乱序和跨范围拒绝符合预期。用输出的 `metadata_daemon_id` 按 [I8 验证说明](msmonitor_center_control.md)查询持久化结果。

## 跨机 HTTPS

在已有的 Linux I3、I8 测试环境启动临时入口。填写测试客户端可达的中心 IPv4。脚本复用现有 HAProxy 镜像，为 I3 写入和查询、I8 注册和心跳提供同一个 HTTPS 地址。

```bash
ip -br -4 addr
CENTER_IP='填中心实际 IPv4'
bash test/st/center/https_gateway.sh "$CENTER_IP"
```

先在中心宿主机验证入口。运行之前确认 I3 的两个测试租户已配置，I8 的两副本均已就绪。若 I3 部署时使用了其他 `--work` 目录，替换写入凭据文件路径。

```bash
ORIGIN="https://$CENTER_IP:18469"
python3 test/st/center/daemon_smoke.py \
  --write-origin "$ORIGIN" --control-origin "$ORIGIN" \
  --ca "$HOME/.local/state/msmonitor-center-https-lab/cert.pem" \
  --write-tokens "$HOME/req2-final/tokens.json" \
  --control-tokens "$HOME/.local/state/msmonitor-i8-lab/secrets/tokens.json" \
  --tenant 10 --scope scope-a
```

跨机验证时，把测试脚本、同目录的 `remote_write_load.py`、CA 证书和对应测试授权的凭据文件放到客户端。替换上述文件路径，再从客户端运行同样的请求。若写入和查询凭据分开，追加 `--query-tokens /path/to/query-tokens.json`。预期 `PASS I8` 和 `PASS I3`。后者验证普通指标、数值摘要和异常样本都可查回。结束后在中心执行 `docker stop msmonitor-center-https-lab`。

## 指标写入吞吐与故障余量

先从回环鉴权入口运行小规模写入回查。压测及回查都使用实验租户 10。

```bash
set -o pipefail
I3_TOKENS="${I3_TOKENS:-$HOME/req2-final/tokens.json}"
python3 test/st/center/remote_write_load.py 127.0.0.1 18467 100 30 8 \
  --tenant 10 --tokens "$I3_TOKENS" --verify-readback \
  | tee "i3-100-$(date +%Y%m%d-%H%M%S).json"
```

记录并发连接、稳态样本/秒、请求体字节/秒、活跃时序数、写入延迟和回查结果。小测通过后逐级增加连接、批内样本或上报频率，先记录 `5100` 连接档。发压机需要足够的文件描述符，先检查 `ulimit -n`。从异机测试时改用 HTTPS 地址和 `--tls --ca`。

在测试环境用下列命令运行 5100 连接档，双写入实例正常时先跑一轮。

```bash
i3_5100() {
  python3 test/st/center/remote_write_load.py 127.0.0.1 18467 5100 30 8 \
    --tenant 10 --tokens "$I3_TOKENS" --verify-readback --readback-timeout 120 \
    | tee "i3-5100-$1-$(date +%Y%m%d-%H%M%S).json"
}
i3_5100 both
```

双实例测试通过后，再执行 `docker stop req2-final-insert1`。用 `docker logs --tail 30 req2-final-l4` 确认写入实例被摘除后执行 `i3_5100 half`。测完执行 `docker start req2-final-insert1`。比较两轮成功样本、稳态吞吐、延迟和完整回查的结果。需要在更高档设硬性吞吐下限时，用 `--min-steady-samples-per-second` 和 `--min-steady-body-bytes-per-second`。

当前 5100 连接、约 7511 样本/秒和 40800 时序来自此前独立测试。十万卡规划还需要单独核对约 9 MB/s 和 500 万活跃时序目标。写入实例的故障余量用同一个目标负载验证，先双实例正常运行，再让半数写入实例停止，待 L4 摘除后重跑同样负载并回查。停实例过程中的切换使用独立的持续写入测试。写入入口的连接数和 vminsert 实际收到的连接数分别记录。

### 持续写入与故障回查

探针在每个测试轮次使用一条独立时序，按固定间隔发送带毫秒时间戳和递增序号的样本，同时轮询原始样本。逐点核对时间戳和值，并测量 HTTP 204 到首次查到样本的时间。写入失败、查询错误和已接收样本的回查超时分别记录。轮次结束时重新读取全段样本，核对故障前已可见的样本在恢复后仍能查到。生产监控可定期调度，按退出状态告警，并保留汇总中的可见延迟和错误计数。

在中心 Linux POC 的终端 A 运行。使用实验租户和现有鉴权入口。

```bash
if (
  set -o pipefail
  I3_TOKENS="${I3_TOKENS:-$HOME/req2-final/tokens.json}"
  python3 -u test/st/center/remote_write_canary.py \
    --origin http://127.0.0.1:18467 --tenant 10 \
    --write-tokens "$I3_TOKENS" --query-tokens "$I3_TOKENS" \
    --interval 2 --duration 180 --visibility-timeout 30 \
    2>&1 | tee "i3-canary-writer-$(date +%Y%m%d-%H%M%S).log"
); then
  printf '%s\n' 'Probe finished successfully'
else
  printf 'Probe failed (exit %s). See the log above.\n' "$?"
fi
```

看到 `first_visible` 后，在终端 B 执行。只操作此 POC 的写入容器。另一写入容器、存储和查询保持运行。

```bash
if (
  set -o pipefail
  (
    for writer in req2-final-insert1 req2-final-insert2; do
      if [ "$(docker inspect --format '{{.State.Running}}' "$writer")" != true ]; then
        printf '%s must be running before the drill.\n' "$writer" >&2
        exit 1
      fi
    done
    trap 'status=$?; docker start req2-final-insert1 || status=1; exit "$status"' EXIT
    fault_started=$(date -Is)
    printf 'fault_start=%s\n' "$fault_started"
    docker stop req2-final-insert1 || exit 1
    printf 'writer_stopped=%s\n' "$(date -Is)"
    sleep 30 || exit 1
    docker logs --since "$fault_started" req2-final-l4 2>&1 || exit 1
    printf 'writer_restart=%s\n' "$(date -Is)"
  ) | tee "i3-writer-window-$(date +%Y%m%d-%H%M%S).log"
); then
  printf '%s\n' 'Fault drill finished and the writer restarted'
else
  printf 'Fault drill failed (exit %s). Check writer status before retrying.\n' "$?"
fi
```

如果终端中途关闭，重新连接后先用 `docker ps -a --filter 'name=req2-final-'` 核对容器状态。正常退出时命令会执行恢复动作，SSH 异常断开后以容器实际状态为准。

用故障时间对照 `sample_visible` 里的样本时间戳，确认故障前、单实例期间和恢复后均有正常写入并可查。默认通过条件为零跳过、零写入和查询失败、零已接收样本回查超时，且最终 `final_verified` 等于 `accepted`，缺失和未预期样本数均为零。临时失败受允许阈值约束时，仍需单独报告失败次数和恢复时间。探针每个样本只发送一次，测的是中心链路的接收与可见性。客户端补传另由真实 daemon 联验。

访问 HTTPS 入口时，将 `--origin` 设置为网关地址，用 `--ca` 指向信任的 CA 文件，并分别配置写入与查询凭据。存储副本退出按 [I3 Kubernetes 演练说明](../install_guide/msmonitor_center_metrics.md)在独立的三存储测试集群操作。当前单存储 Docker 环境用于写入实例故障演练。

### 9 MB/s 带宽和半数写入实例故障

在中心 Linux 测试机依次运行。脚本走回环鉴权入口和实验租户 10。小档用于验证每批 82 条样本的写入和回查。

```bash
bash test/st/center/remote_write_bandwidth.sh smoke &&
bash test/st/center/remote_write_bandwidth.sh target
```

目标档为 5100 个客户端，每个每 5 秒写一批，稳态至少 9,000,000 请求体字节/秒。先验证两个写入实例正常的情况，随后停止 `req2-final-insert1`，记录容器状态并重跑同档。子任务结束后会恢复该容器。检查日志目录中 `both.json`、`half.json` 的 `passed` 均为 `true`，峰值连接均为 5100，均查回 418200 条时序，且错误为空。字节数按 Remote Write 请求体计算。该结果验证所测连接档下的稳态承载能力。

### 500 万活跃时序

在有足够存储和内存的实验环境逐级发压，每档等待回查通过再继续。每档写入独立的样本集，数据随测试库保留周期过期。工具按组核对独立时序数和样本数。

```bash
set -o pipefail
I3_TOKENS="${I3_TOKENS:-$HOME/req2-final/tokens.json}"

i3_cardinality() {
  python3 test/st/center/test_series_cardinality.py 127.0.0.1 18467 "$1" \
    --tenant 10 --tokens "$I3_TOKENS" --max-series-per-second 20000 \
    | tee "i3-cardinality-$1-$(date +%Y%m%d-%H%M%S).json"
}

i3_cardinality 100000
# 前一档通过后按需执行下一档。
i3_cardinality 1000000
i3_cardinality 5000000
```

记录每档已接收时序数、完整回查数、写入与回查耗时、CPU、内存和存储。500 万档要求目标时序和样本均全部查回。带宽与时序基数的分项结果分别记录，目标 K8s 环境还应验证两项并存时的容量。

## 节点注册和心跳负载

两副本均就绪后先跑小档。测试分开记录注册和心跳阶段。

```bash
set -o pipefail
I8_TOKENS="$HOME/.local/state/msmonitor-i8-lab/secrets/tokens.json"
python3 test/st/center/test_control_service_load.py \
  --origin-a http://127.0.0.1:18471 --origin-b http://127.0.0.1:18472 \
  --tokens "$I8_TOKENS" --daemons 1000 --interval 15 --cycles 3 \
  | tee "i8-1000-$(date +%Y%m%d-%H%M%S).json"
```

小档通过后，按相同方式提高到 5000、12500 个 daemon。按每 15 秒心跳估算，12500 对应约 833 次/秒。确认计划请求数与成功数一致，关注实际发送速率、调度滞后和 P95/P99 延迟。必要时将客户端迁到独立机器，避免发压端与服务争抢资源。生产容量仍以目标部署的资源配额和实测为准。

## 节点控制服务凭据轮转

只在现有 Linux 实验 Compose 环境执行。保持其他测试停止，轮转期间应用副本逐个重建。先在终端 1 准备受限权限的凭据文件。脚本会保存原始文件，给 `scope-a` 生成新 Token。

```bash
umask 077
I8_SECRETS="$HOME/.local/state/msmonitor-i8-lab/secrets"
ROTATION_DIR=$(mktemp -d "$I8_SECRETS/.rotation.XXXXXX")
python3 - "$I8_SECRETS/tokens.json" "$ROTATION_DIR" <<'PY'
import json
import secrets
import sys
from pathlib import Path

source, directory = map(Path, sys.argv[1:])
original = source.read_text(encoding="utf-8")
tokens = json.loads(original)
old = tokens["scope-a"]
if not isinstance(old, str):
    raise SystemExit("Restore the original single-token file before retrying.")
new = secrets.token_hex(24)
(directory / "original.json").write_text(original, encoding="utf-8")
(directory / "dual.json").write_text(json.dumps({**tokens, "scope-a": [old, new]}), encoding="utf-8")
(directory / "new.json").write_text(json.dumps({**tokens, "scope-a": new}), encoding="utf-8")
PY
printf 'ROTATION_DIR=%s\n' "$ROTATION_DIR"
python3 test/st/center/test_control_token_rotation.py \
  --origin-a http://127.0.0.1:18471 --origin-b http://127.0.0.1:18472 \
  --tokens "$ROTATION_DIR/dual.json" --scope scope-a
```

在终端 2 准备以下命令，将 `ROTATION_DIR` 改成终端 1 打印的实际目录。

```bash
I8_SECRETS="$HOME/.local/state/msmonitor-i8-lab/secrets"
ROTATION_DIR='填终端 1 打印的目录'
install_i8_tokens() {
  local temp_file
  temp_file=$(mktemp "$I8_SECRETS/.tokens.XXXXXX") &&
  install -m 600 "$ROTATION_DIR/$1.json" "$temp_file" &&
  mv -f "$temp_file" "$I8_SECRETS/tokens.json"
}
recreate_i8() {
  bash deploy/center/control/lab.sh compose up -d --no-deps --force-recreate --wait "$1"
}
```

按终端 1 的提示依次执行。上一阶段通过后再执行下一条。

| 提示 | 终端 2 命令 |
| --- | --- |
| Install the dual-token file | `install_i8_tokens dual && recreate_i8 app1` |
| Recreate app2 with the same dual-token file | `recreate_i8 app2` |
| Install the new-only token file | `install_i8_tokens new && recreate_i8 app1` |
| Recreate app2 with the same new-only token file | `recreate_i8 app2` |

预期五条 `PASS`。验证原会话持续可用、两副本均拒绝已撤销的旧 Token、其他授权范围保持隔离。测完或中途失败时恢复原始测试凭据，避免影响之前的联调客户端。

```bash
install_i8_tokens original && recreate_i8 app1 && recreate_i8 app2
```
