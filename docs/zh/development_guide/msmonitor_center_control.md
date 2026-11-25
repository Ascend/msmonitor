# 节点注册与控制服务（beta）

节点注册与控制服务（I8）处理注册、心跳、任务领取和结果回报。`deploy/center/control/` 提供统一中心镜像的构建文件和 Kubernetes 安装器；完整中心部署见[中心部署说明](../install_guide/msmonitor_center.md)。

`app.py` 提供 daemon 注册和心跳接口。`registry.py` 在 PostgreSQL 中共享会话和最近状态。两个 API 副本之间可以重试同一会话。首次注册、实例替换和心跳成功均在事务提交后返回。

## 控制服务单元测试

在仓库根目录、安装了控制服务 Python 依赖的环境中运行。用例直接调用 API 处理函数并模拟数据库返回值，无需启动 Docker 或 PostgreSQL。实际事务、跨副本和故障恢复由下文系统测试验证。

```bash
python3 -m pip install -r deploy/center/control/requirements.txt
python3 -m unittest discover -s test/ut/center
```

## Linux 验证

单容器镜像与本节双副本实验默认均使用 PostgreSQL 15。已有 PostgreSQL 17 实验卷时，先执行 `export I8_LAB_POSTGRES_IMAGE=postgres:17-bookworm` 再启动原项目。验证默认 15 时，使用新的 Compose 项目（`I8_LAB_PROJECT`）和空卷，并错开原项目的宿主机端口。

在仓库根目录执行以下命令。需要 Python 3、Docker 和 Docker Compose。测试使用单独的 Compose 项目和回环端口 `18471`、`18472`。实验凭据在 `~/.local/state/msmonitor-i8-lab/secrets` 自动生成。

```bash
bash deploy/center/control/lab.sh up
bash deploy/center/control/lab.sh test --wait-for-failover --wait-for-db-recovery --transition-timeout 180
```

在同一目录的另一个终端，按测试提示依次停止副本 A、停止数据库、启动数据库。等上一项检查通过再执行下一条。

```bash
bash deploy/center/control/lab.sh compose stop app1
bash deploy/center/control/lab.sh compose stop db
bash deploy/center/control/lab.sh compose start db
```

检查四条 `PASS`。两副本返回相同的注册会话，跨授权范围访问被拒绝，旧实例不能继续上报。停掉副本 A 后 B 能接手同一会话。停库期间 B 的 `/livez` 返回 200，`/readyz` 和心跳返回 503。库恢复后原会话可继续用。

用输出中的 `daemon_id` 查数据库，确认恢复后的状态已持久化。完整故障测试结束后，`scope-a` 的活动实例为 `boot-2`，序号为 4，状态为 `running`。如果只运行基础测试，预期序号为 2，状态仍为 `running`，可确认最后发送的重复和乱序心跳没有覆盖状态。

```bash
DAEMON_ID='填本次测试输出的 daemon_id'
bash deploy/center/control/lab.sh compose exec -T db psql -U i8 -d i8 -v daemon_id="$DAEMON_ID" <<'SQL'
SELECT scope, instance_id, status_seq, collect_state, online
FROM msmonitor_i8.current_daemons WHERE daemon_id = :'daemon_id' ORDER BY scope;
SQL
bash deploy/center/control/lab.sh compose start app1
```

`online` 由最后一次有效心跳时间计算，达到 60 秒未上报后转为离线。应用指标在 `/metrics`，包含请求量、延迟、数据库连接数和连接池等待。Compose 实验能检查单副本退出和数据库恢复，部署容量需在目标资源配额下另行压测。

## 扩展注册和心跳

注册和心跳都可携带 `metadata` 对象。现有必填字段、响应、Token 授权范围和 4 KiB 请求体上限保持兼容。元数据只用于运维关联，授权范围由 Bearer Token 决定。

| 请求 | 可选字段 | 校验 |
| --- | --- | --- |
| 注册 | `job_id`、`run_id`、`node_id`、`daemon_version` | 1～128 个 ASCII 字母、数字、下划线、点或连字符 |
| 注册 | `rank_capabilities` | 最多 64 个唯一标识，单个标识规则同上 |
| 心跳 | `app_version` | 同注册标识规则 |
| 心跳 | `pending_files`、`dropped_records`、`rss_bytes` | 0 至 2⁶³−1 的整数，分别表示积压文件数、丢弃记录数和常驻内存字节数 |
| 心跳 | `cpu_percent` | 0 至 100 的数字，使用节点容量归一化后的 CPU 占比 |

首次注册保存注册元数据。重复注册返回原会话且保留首次状态。只有序号递增的有效心跳才更新会话，显式 `metadata` 替换遥测快照，缺省时保留旧快照和它的序号。空对象表示清空该次遥测。`metadata` 内未知字段或类型错误返回 400。

### 验证元数据及持久化

在仓库根目录执行。依次验证旧协议、扩展字段的限制、跨副本重复注册、心跳乱序与跨范围拒绝。

```bash
bash deploy/center/control/lab.sh up
bash deploy/center/control/lab.sh test --verify-metadata
```

使用输出里的 `metadata_daemon_id` 查询状态。将示例变量替换为实际 ID，结果应返回一行且 `persisted` 为 `t`。状态序号 3 由旧式心跳推进，遥测仍对应带元数据的序号 2。

```bash
DAEMON_ID='填扩展测试输出的 metadata_daemon_id'
bash deploy/center/control/lab.sh compose exec -T db psql -X -v ON_ERROR_STOP=1 -U i8 -d i8 -v daemon_id="$DAEMON_ID" <<'SQL'
SELECT
    registration_metadata = '{
        "job_id": "job-a", "run_id": "run-a", "node_id": "node-a",
        "daemon_version": "v1.0", "rank_capabilities": ["rank-0", "rank-1"]
    }'::jsonb
    AND heartbeat_metadata = '{"pending_files": 4}'::jsonb
    AND heartbeat_metadata_seq = 2 AND status_seq = 3 AS persisted
FROM msmonitor_i8.daemon_details
WHERE scope = 'scope-a' AND daemon_id = :'daemon_id';
SQL
```

## 中心任务领取及结果回报

中心运维通过受限数据库凭据创建、查询任务。daemon 的 HTTP 凭据只允许领取和回报。每个任务针对一个 scope、daemon 和 rank，以 request_id 和正整数 version 标识同一次操作。同键同参数可重复创建，变更参数需使用新版本。collect 需指定 duration-s，stop 仅需 rank，export 需指定半开纳秒时间窗。命令应绑定到本次运行独有的 scope 或 daemon_id。

```bash
bash deploy/center/control/lab.sh compose exec -T app1 python -m registry enqueue \
  --scope scope-a --daemon-id node-01 --request-id export-1 --version 1 \
  --kind export --rank 0 --start-ns 100 --end-ns 200
bash deploy/center/control/lab.sh compose exec -T app1 python -m registry show \
  --scope scope-a --daemon-id node-01 --request-id export-1 --version 1
```

两个 HTTPS POST 都使用注册凭据，正文为 JSON。

- `/api/v1/daemon/poll-commands` 接收 session_id 和可选 wait_s（0～20，默认 20 秒）。响应 command 为 null 或包含 request_id、version、kind、rank 和执行参数的对象。
- `/api/v1/daemon/ack` 接收 session_id、request_id、version、result。result 包含 rank、status（success、partial、failed），可附 uri、sha256 和 missing_ranges_ns（纳秒半开区间列表）。导出成功必须附产物 URI 和 SHA-256。中心保存引用，原始文件留在节点。
- 领取后到确认前，同一任务可重复返回。同一 rank 的 collect 已领取且尚未确认时，后续 stop 优先交付；stop 确认后仍可重领并确认原 collect。daemon 按任务键持久化执行记录，再次领取时复用结果，并遵守本地资源限制和 stop 优先。中心提交结果后返回 accepted；相同结果重试成功，冲突结果或被替换的会话返回 409。故障后新会话需重新领取未确认任务。

```bash
bash deploy/center/control/lab.sh up
bash deploy/center/control/lab.sh test --verify-commands
```

运行角色对 commands 表需 SELECT/UPDATE，创建任务的中心运维角色需 SELECT/INSERT。实验环境共用数据库角色。已确认任务留存供运维查询；清理时需考虑重试期限。

## 注册发现与结构化摘要

平台配置 `public_write_origin`（外部 HTTPS origin）及 `scope_accounts`（例如 `{"demo-job":"10"}`）后，注册响应附加该 scope 的 `remote_write_url`。未绑定的 scope 沿用显式写入 URL；daemon 显式配置优先。控制与写入 Token 分别由平台挂载到节点并告知 daemon 各自的文件路径。中心注册响应不分发 Token。心跳周期由响应字段 heartbeat_interval_s 指定，CA 由部署方提供。长轮询满额时中心即时检查任务，空队列可随 command:null 返回 retry_after_s，客户端应等待并加随机抖动。

`POST /api/v1/daemon/submit-summary` 使用当前会话及控制 Bearer Token。请求示例：

```json
{"session_id":"注册所得值","batch_id":"window-001","schema_version":1,"records":[{"rank":0,"window_end_ms":1794727894073,"operator":"MatMul","p99":0.012}]}
```

业务对象由摘要方定义；每批 1～256 个非空对象，UTF-8 JSON 正文最多 256 KiB。daemon 将批次及其稳定 ID 缓存至收到 HTTP 200 和 `{"persisted":true,"batch_id":"window-001","count":1}`。数据库按 scope、daemon_id、batch_id 唯一保存，实例替换后可在新会话中重试。相同批次内容冲突返回 409，当前仅接收版本 1。越权 403、替换会话 409、格式/版本错误 400、过大 413、库故障 503。运行角色对 summary_batches 表需 SELECT/INSERT。内部授权查询用 convert_from(records,'UTF8')::jsonb，按 scope 过滤。批次按业务留存期归档，清理需覆盖客户端允许的重试时间。

## 部署配置

- `Dockerfile` 构建运行镜像。`k8s-migrate.yaml` 和 `k8s.yaml` 提供独立迁移 Job 和双副本服务模板。将镜像改为实际发布镜像，配置目标命名空间的 Secret，创建迁移 Job 并等待完成，再部署 API。
- 迁移角色需有创建 schema 和表的权限。会话功能需要 `msmonitor_i8` schema 的 `USAGE` 和 `daemon_sessions` 表的 `SELECT/INSERT/UPDATE` 权限。运维只读角色可通过 `current_daemons` 查询在线状态，经单独授权的内部只读角色可通过 `daemon_details` 查询元数据。迁移保留旧视图结构，先迁移再滚动应用。全部副本升级后再让 daemon 发送扩展字段。旧副本会忽略扩展字段，混合版本期间不要用扩展字段验收持久化。数据库高可用、备份和生产连接加密由部署环境配置。
- 在 HTTPS 入口发布注册、心跳、任务领取和结果回报路径。`/livez`、`/readyz`、`/metrics` 保留在内部。Token 文件每个授权范围可配置一个凭据，轮转期间可配置新旧两个，修改 Secret 后滚动重启，再让客户端切换并移除旧凭据。节点需在心跳和重试中增加随机抖动。
- 历史会话目前保留，用于拒绝已经被替换的实例。清理应在任务授权撤销后按保留策略执行。

模拟 daemon 客户端位于 `test/st/center/daemon_smoke.py`，编码辅助与负载工具位于同目录的 `remote_write_load.py`。在仓库根目录执行 `python3 test/st/center/daemon_smoke.py --help` 查看参数。该客户端通过 HTTPS 检查注册重试、心跳、旧会话隔离，以及 Remote Write 样本写入后的回查。测试工具与中心服务分别维护。
