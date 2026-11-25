"""Loopback API behind the authenticated Grafana data-source proxy."""

import json
import logging
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from statistics import median
from threading import BoundedSemaphore
from urllib.parse import parse_qs, urlsplit

from prometheus import Prometheus, PrometheusError
from summary import DOMAIN_TYPES, OPERATOR_KINDS, Summary


def setting_metric(name):
    value = os.environ.get(name, "")
    if value and not re.fullmatch(r"[a-zA-Z_:][a-zA-Z0-9_:]*", value):
        raise ValueError(f"{name} must name a Prometheus metric")
    return value


def milliseconds(text):
    value = int(text)
    if value < 0:
        raise ValueError("时间范围无效")
    return value


def integers(text):
    if not text.strip():
        return set()
    values = {int(part) for part in re.split(r"[ ,，]+", text.strip()) if part}
    if any(value < 0 for value in values):
        raise ValueError("Step 和 Rank 必须为非负整数")
    return values


def table(rows, layout, domain_type, search, sort, descending, page):
    fields = {
        "tail": (
            "group",
            "type",
            "count",
            "median",
            "fast",
            "fastMs",
            "slow",
            "slowMs",
            "positive",
            "negative",
            "tail",
        ),
        "offset": ("group", "type", "count", "median", "baseline", "offset", "rate"),
    }[layout]
    if sort not in fields:
        sort = layout
    rows = [
        row
        for row in rows
        if (not domain_type or row["type"] == domain_type) and search.lower() in row["group"].lower()
    ]
    defined = [row for row in rows if row[sort] is not None]
    undefined = [row for row in rows if row[sort] is None]
    defined.sort(key=lambda row: row["group"])
    defined.sort(key=lambda row: row[sort], reverse=descending)
    ordered = defined + sorted(undefined, key=lambda row: row["group"])
    page = max(1, min(page, (len(ordered) + 19) // 20))
    maximum = max((abs(row[sort]) for row in rows if isinstance(row[sort], (int, float))), default=0)
    return {
        "total": len(ordered),
        "page": page,
        "scale": maximum,
        "rows": [{field: row[field] for field in fields} for row in ordered[(page - 1) * 20 : page * 20]],
    }


def rank_offset_rows(offsets, direction, limit):
    if direction == "positive":
        ordered = sorted((row for row in offsets if row[2] > 0), key=lambda row: (-row[2], row[0]))
    elif direction == "negative":
        ordered = sorted((row for row in offsets if row[2] < 0), key=lambda row: (row[2], row[0]))
    else:
        ordered = sorted(offsets, key=lambda row: (-abs(row[2]), row[0]))
    return [{"rank": rank, "ms": duration, "offset": offset} for rank, duration, offset in ordered[:limit]]


def rank_result(rows, group, basis, kind, direction, limit, threshold):
    domain = next((row for row in rows if row["group"] == group), None)
    result = {"rows": [], "count": 0, "baseline": None, "positive": 0, "negative": 0, "status": ""}
    if domain is None:
        return result
    baseline = domain["baseline" if basis == "peers" else "median"]
    result.update(count=domain["count"], baseline=baseline)
    if baseline is None:
        result["status"] = "无可比基线"
        return result
    offsets = [(rank, duration, duration - baseline) for rank, duration in domain["values"].items()]
    result["positive"] = sum(offset > threshold for _, _, offset in offsets)
    result["negative"] = sum(offset < -threshold for _, _, offset in offsets)
    direction = direction or ("positive" if kind == "Kernel" else "both")
    result["rows"] = rank_offset_rows(offsets, direction, limit)
    return result


def dispatch(summary, path, params):
    def arg(key, default=""):
        return params.get(key, [default])[0]

    if path == "health":
        return {"ok": True}

    scope = {"job_name": arg("job"), "run_id": arg("run")}
    if not all(scope.values()):
        raise ValueError("请选择 job_name 和 run_id")
    if path == "recent":
        return summary.recent(scope)

    start, end = milliseconds(arg("from")), milliseconds(arg("to"))
    if start > end:
        raise ValueError("时间范围无效")
    if path == "overview":
        return {"rows": summary.overview_rows(scope, start, end)}
    if path == "rank-trend":
        rank = milliseconds(arg("rank"))
        return {"rows": summary.rank_trend(scope, start, end, rank, arg("all") == "true")}
    if path == "groups":
        step = milliseconds(arg("step")) if arg("step") else None
        return {"groups": summary.group_list(scope, start, end, step)}
    if path in {"ranges", "coverage"}:
        return summary.l2(
            scope,
            start,
            end,
            path,
            arg("group"),
            integers(arg("steps")),
            integers(arg("ranks")),
            set(params.get("name", [])),
            arg("source"),
        )

    kind = arg("kind", "Kernel")
    if path in {"operator-trend-options", "operator-trend", "operator-trends"}:
        rank = arg("rank")
        if rank and (not rank.isascii() or not rank.isdecimal()):
            raise ValueError("Rank 必须为非负整数")
        if path == "operator-trend-options":
            return summary.operator_trend_options(scope, start, end, rank)
        if kind not in OPERATOR_KINDS:
            raise ValueError("算子类别无效")
        operators = [arg("operator")] if path == "operator-trend" else json.loads(arg("operators", "[]"))
        if (
            not isinstance(operators, list)
            or not operators
            or not all(isinstance(name, str) and name for name in operators)
        ):
            raise ValueError("请选择算子类型")
        selected = tuple(dict.fromkeys(operators))
        series = summary.operator_trends(scope, start, end, kind, rank, selected)
        return {"rows": series[selected[0]]} if path == "operator-trend" else {"series": series}

    step = milliseconds(arg("step"))
    if path == "communication-ranks":
        totals = summary.communication_rank_totals(scope, start, end, step, arg("operator"))
        if not totals:
            return {"rows": [], "count": 0, "baseline": None}
        baseline = median(totals.values())
        offsets = [(rank, duration, duration - baseline) for rank, duration in totals.items()]
        return {
            "rows": rank_offset_rows(offsets, "both", 20 if arg("limit") == "20" else 10),
            "count": len(totals),
            "baseline": baseline,
        }

    if kind not in OPERATOR_KINDS:
        raise ValueError("算子类别无效")
    if path in {"kde-options", "kde"}:
        rank = arg("rank")
        if not rank and path == "kde-options":
            return summary.kde_options(scope, start, end, step, kind)
        if not rank.isascii() or not rank.isdecimal():
            raise ValueError("Rank 必须为非负整数")
        if path == "kde-options":
            return summary.kde_options(scope, start, end, step, kind, rank)
        return {"profiles": summary.kde(scope, start, end, step, kind, rank, arg("operator"))}
    if path == "operators":
        names, _ = summary.operators(scope, start, end, step, kind)
        return {"operators": names}
    if path in {"tail", "offset", "top-ranks"}:
        rows, status, _ = summary.domains(scope, start, end, step, kind, arg("operator") if path == "top-ranks" else "")
        if path == "top-ranks":
            threshold = float(arg("threshold", "10"))
            if not 0 <= threshold < float("inf"):
                raise ValueError("偏移阈值必须为非负数字")
            result = rank_result(
                rows, arg("group"), arg("basis"), kind, arg("direction"), 20 if arg("limit") == "20" else 10, threshold
            )
            if status and not result["status"]:
                result["status"] = status
            return result

        domain_type = arg("type")
        if domain_type and domain_type not in DOMAIN_TYPES:
            raise ValueError("通信域类型无效")
        result = table(
            rows,
            path,
            domain_type,
            arg("search"),
            arg("sort", path),
            arg("desc", "true") == "true",
            int(arg("page", "1")),
        )
        return {**result, "status": status}
    raise LookupError(path)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        request = urlsplit(self.path)
        path = request.path.strip("/").removeprefix("summary/")
        try:
            with self.server.requests:
                result = dispatch(self.server.summary, path, parse_qs(request.query, max_num_fields=128))
            self.respond(200, result)
        except ValueError as error:
            self.respond(400, {"message": str(error)})
        except LookupError:
            self.respond(404, {"message": "查询不存在"})
        except PrometheusError as error:
            self.respond(502, {"message": str(error)})
        except Exception:
            logging.exception("Step query failed at %s", path)
            self.respond(500, {"message": "Step 查询失败，请查看服务日志"})

    def respond(self, code, result):
        body = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    # Preserve the BaseHTTPRequestHandler keyword parameter name.
    def log_message(self, format, *args):  # pylint: disable=redefined-builtin
        if len(args) > 1 and str(args[1]).startswith(("4", "5")):
            logging.warning("%s %s", urlsplit(self.path).path, args[1])


def main():
    wait = os.environ.get("MSMONITOR_REPORT_WAIT_SECONDS", "")
    if wait and (not wait.isdecimal()):
        raise ValueError("MSMONITOR_REPORT_WAIT_SECONDS must be a nonnegative integer")
    summary = Summary(
        Prometheus(),
        setting_metric("MSMONITOR_EXPECTED_RANKS_METRIC"),
        setting_metric("MSMONITOR_RECEIVED_TIMESTAMP_METRIC"),
        setting_metric("MSMONITOR_RUN_STATUS_METRIC"),
        int(wait) if wait else None,
    )
    with ThreadingHTTPServer(("127.0.0.1", int(os.environ.get("MSMONITOR_SUMMARY_PORT", "19103"))), Handler) as server:
        server.summary = summary
        server.requests = BoundedSemaphore(12)
        server.serve_forever()


if __name__ == "__main__":
    main()
