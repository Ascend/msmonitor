"""Step-based aggregations shared by the three dashboards."""

from collections import Counter, defaultdict
from functools import lru_cache
from math import floor
from re import escape
from statistics import median
from threading import Lock
from time import time

from prometheus import DAY, HOUR, PREFIX, selector

STEP = PREFIX + "step_index"
DURATION = PREFIX + "iteration_duration_seconds"
VALID = PREFIX + "step_summary_valid"
GROUP = PREFIX + "comm_group_member"
COVERAGE = PREFIX + "activity_coverage_ratio"
RANGE_SUM = PREFIX + "range_time_sum_seconds"
RANGE_CALLS = PREFIX + "range_calls"
OP_STEP = PREFIX + "operator_step_id"
OP_TOTAL = PREFIX + "operator_duration_total_seconds"
OP_CALLS = PREFIX + "operator_calls"
CLUSTER = PREFIX + "operator_cluster_"
CLUSTER_FIELDS = {
    "calls": "calls",
    "min_seconds": "min",
    "max_seconds": "max",
    "p50_seconds": "p50",
    "p99_seconds": "p99",
}
DOMAIN_TYPES = {"dp", "pp", "tp"}
OPERATOR_KINDS = ("Kernel", "Communication")


def rank_id(labels):
    rank = labels.get("rank", "")
    return int(rank) if rank.isascii() and rank.isdecimal() else None


def operator_samples(operators, operator):
    for identity, (_, duration) in operators.items():
        labels = dict(identity)
        if not operator or labels.get("operator_type") == operator:
            yield labels, duration


def rank_totals(samples):
    totals = Counter()
    for labels, duration in samples:
        totals[rank_id(labels)] += duration
    return totals


def step_id(value):
    return int(value) if value >= 0 and value.is_integer() else None


def aligned_operator_durations(rows):
    steps = {
        (frozenset(labels.items()), stamp): step
        for metric, labels, stamp, value in rows
        if metric == OP_STEP and (step := step_id(float(value))) is not None
    }
    for metric, labels, stamp, value in rows:
        if metric != OP_TOTAL or value < 0 or rank_id(labels) is None:
            continue
        step = steps.get((frozenset(labels.items()), stamp))
        if step is not None:
            yield step, labels, stamp, value * 1000


def step_keys(rows):
    return {
        (labels.get("rank"), stamp): step
        for metric, labels, stamp, value in rows
        if metric == STEP and (step := step_id(value)) is not None
    }


def durations(rows, keys):
    invalid = {(labels.get("rank"), stamp) for metric, labels, stamp, value in rows if metric == VALID and value <= 0}
    for metric, labels, stamp, value in rows:
        if metric != DURATION or value < 0 or (rank := rank_id(labels)) is None:
            continue
        key = labels["rank"], stamp
        if key in keys and key not in invalid:
            yield keys[key], rank, stamp, value * 1000


def newer(mapping, key, stamp, value):
    if key not in mapping or stamp >= mapping[key][0]:
        mapping[key] = stamp, value


def fresh_bucket():
    return {"count": 0, "_sum": 0.0, "fast": None, "slow": None, "_from": None, "_to": None}


def observe_stamp(bucket, stamp):
    bucket["_from"] = stamp if bucket["_from"] is None else min(bucket["_from"], stamp)
    bucket["_to"] = stamp if bucket["_to"] is None else max(bucket["_to"], stamp)


def add_rank(bucket, rank, duration):
    bucket["count"] += 1
    bucket["_sum"] += duration
    fast, slow = bucket["fast"], bucket["slow"]
    if fast is None or (duration, rank) < (fast["ms"], fast["rank"]):
        bucket["fast"] = {"rank": rank, "ms": duration}
    if slow is None or (-duration, rank) < (-slow["ms"], slow["rank"]):
        bucket["slow"] = {"rank": rank, "ms": duration}


def summarize_reporting(bucket, expected, received, wait_seconds, now):
    count = bucket["count"]
    missing = None
    status = "应报数未知"
    if expected is not None:
        if count >= expected:
            missing, status = 0, "已收齐" if count == expected else "实报超过应报"
        elif received is None or wait_seconds is None:
            status = "上报状态未知"
        elif now < received + wait_seconds:
            status = "上报中"
        else:
            missing, status = expected - count, "缺报"
    return {
        "step": bucket["step"],
        "count": count,
        "expected": expected,
        "missing": missing,
        "status": status,
        "mean": bucket["_sum"] / count if count else None,
        "spread": bucket["slow"]["ms"] - bucket["fast"]["ms"] if count >= 2 else None,
        "fast": bucket["fast"],
        "slow": bucket["slow"],
    }


def comparable_baselines(rows):
    """Use each peer domain once and exclude the domain under evaluation."""
    cohorts = defaultdict(list)
    for row in rows:
        if row["cohort"] is not None:
            cohorts[row["cohort"]].append((row["median"], row["group"]))
    result = {}
    for values in cohorts.values():
        values.sort()
        remaining = len(values) - 1
        if not remaining:
            continue

        lower = (remaining - 1) // 2
        upper = remaining // 2
        for removed, (_, group) in enumerate(values):
            low_value = values[lower + (lower >= removed)][0]
            high_value = values[upper + (upper >= removed)][0]
            result[group] = (low_value + high_value) / 2
    return result


class Summary:
    def __init__(self, prom, expected_metric="", received_metric="", state_metric="", wait_seconds=None):
        self.prom = prom
        self.expected_metric = expected_metric
        self.received_metric = received_metric
        self.state_metric = state_metric
        self.wait_seconds = wait_seconds
        self.overview_locks = [Lock() for _ in range(16)]

    def partitions(self, scope, start, end):
        ranks = self.prom.labels(STEP, "rank", scope, start, end)
        concrete = sorted({int(rank) for rank in ranks if rank.isascii() and rank.isdecimal()})
        if len(concrete) <= 2048:
            yield {**scope, "rank!=": "all"}
            return
        for index in range(0, len(concrete), 2048):
            batch = concrete[index : index + 2048]
            yield {**scope, "rank=~": "0*(" + "|".join(map(str, batch)) + ")"}

    @lru_cache(maxsize=24)
    def _overview(self, scope_key, start, end, version):
        scope = dict(scope_key)
        buckets = defaultdict(fresh_bucket)
        expected = {}
        received = {}
        rank_metrics = [STEP, DURATION, VALID]
        if self.received_metric:
            rank_metrics.append(self.received_metric)
        for partition in self.partitions(scope, start, end):
            by_step = defaultdict(dict)
            for rows in self.prom.windows(rank_metrics, partition, start, end):
                keys = step_keys(rows)
                for (_, stamp), step in keys.items():
                    observe_stamp(buckets[step], stamp)
                for step, rank, stamp, duration in durations(rows, keys):
                    newer(by_step[step], rank, stamp, duration)
                for metric, labels, stamp, value in rows:
                    key = (labels.get("rank"), stamp)
                    if self.received_metric and metric == self.received_metric and key in keys and value > 0:
                        step = keys[key]
                        received[step] = min(value, received.get(step, value))
            for step, ranks in by_step.items():
                for rank, (_, duration) in ranks.items():
                    add_rank(buckets[step], rank, duration)

        extras = [metric for metric in (self.expected_metric, self.received_metric) if metric]
        matchers = {**scope, "rank=~": "all|"}
        for rows in self.prom.windows([STEP, *extras], matchers, start, end, width=12 * HOUR):
            keys = {stamp: step for (_, stamp), step in step_keys(rows).items()}
            for stamp, step in keys.items():
                if step not in buckets:
                    observe_stamp(buckets[step], stamp)
            for metric, labels, stamp, value in rows:
                if metric == STEP:
                    continue
                labelled_step = labels.get("step", "")
                step = int(labelled_step) if labelled_step.isdecimal() else keys.get(stamp)
                if step is None:
                    continue
                if step not in buckets:
                    observe_stamp(buckets[step], stamp)
                if metric == self.expected_metric and value >= 0 and value.is_integer():
                    newer(expected, step, stamp, int(value))
                elif metric == self.received_metric and value > 0:
                    received[step] = min(value, received.get(step, value))

        for step, bucket in buckets.items():
            bucket.update(step=step, _expected=expected.get(step, (0, None))[1], _received=received.get(step))
        return buckets

    def overview(self, scope, start, end):
        key = tuple(sorted(scope.items())), start, end
        with self.overview_locks[hash(key) % len(self.overview_locks)]:
            return self._overview(*key, floor(time() / 10))

    def overview_rows(self, scope, start, end):
        buckets = self.overview(scope, start, end)
        now = time()
        return [
            summarize_reporting(
                buckets[step], buckets[step]["_expected"], buckets[step]["_received"], self.wait_seconds, now
            )
            for step in sorted(buckets)
        ]

    def recent(self, scope):
        now = round(time() * 1000)
        data = {"received": None, "state": None}
        for metric, field in ((self.received_metric, "received"), (self.state_metric, "state")):
            if not metric:
                continue
            expression = f"max(last_over_time({selector([metric], scope)}[7d]))"
            rows = self.prom.vector(expression, now)
            if rows:
                value = float(rows[0]["value"][1])
                if field == "received" and 0 < value <= now / 1000:
                    data[field] = value
                if field == "state":
                    data[field] = {0: "已结束", 1: "运行中", 2: "失败"}.get(value)
        return data

    def rank_trend(self, scope, start, end, rank, all_steps=False):
        if all_steps:
            end = (int(time()) // 10 * 10 + 10) * 1000
            start = end - 7 * DAY
        buckets = self.overview(scope, start, end)
        rank_values = {}
        for rows in self.prom.windows([STEP, DURATION, VALID], {**scope, "rank": str(rank)}, start, end):
            for step, _, stamp, duration in durations(rows, step_keys(rows)):
                newer(rank_values, step, stamp, duration)
        return [
            {
                "step": step,
                "mean": bucket["_sum"] / bucket["count"] if bucket["count"] else None,
                "duration": rank_values.get(step, (0, None))[1],
            }
            for step, bucket in sorted(buckets.items())
        ]

    @lru_cache(maxsize=24)
    def _topology(self, scope_key, start, end, version):
        snapshots = defaultdict(dict)
        for _, labels, stamp, value in self.prom.rows([GROUP], dict(scope_key), start - 7 * DAY, end + 300_000):
            rank = rank_id(labels)
            group = labels.get("comm_group")
            if rank is None or not group or value <= 0:
                continue
            domains = snapshots[stamp]
            domains.setdefault(group, {"type": labels.get("group_name", "").lower(), "ranks": set()})["ranks"].add(rank)
        return snapshots

    def topology(self, scope, start, end, at=None):
        snapshots = self._topology(tuple(sorted(scope.items())), start, end, floor(time() / 30))
        if not snapshots:
            return {}
        at = at if at is not None else end
        return snapshots[max((stamp for stamp in snapshots if stamp <= at), default=min(snapshots))]

    def group_list(self, scope, start, end, step=None):
        at = None
        if step is not None:
            buckets = self.overview(scope, start, end)
            if step not in buckets:
                return []
            at = buckets[step]["_to"]
        groups = self.topology(scope, start, end, at)
        return [{"id": group, "type": value["type"]} for group, value in sorted(groups.items())]

    def l2(self, scope, start, end, mode, group, steps, ranks, names, source):
        samples = {}
        topology = {}
        metrics = [COVERAGE] if mode == "coverage" else [RANGE_SUM, RANGE_CALLS]
        available = self.prom.labels(RANGE_SUM, "range_name", scope, start, end) if mode == "ranges" else []
        filters = dict(scope)
        if mode == "coverage":
            filters["scope=~"] = "overlap|uncovered_communication|communication|kernel_active|observed_free"
        if source and mode == "ranges":
            filters["source"] = source
        for rows in self.prom.windows([STEP], scope, start, end):
            for (rank, stamp), step in step_keys(rows).items():
                if not rank or not rank.isdecimal() or (steps and step not in steps):
                    continue
                if ranks and int(rank) not in ranks:
                    continue
                samples[rank, stamp] = step
        combined = {}
        for rows in self.prom.windows(metrics, filters, start, end):
            for metric, labels, stamp, value in rows:
                rank = rank_id(labels)
                step = samples.get((labels.get("rank"), stamp))
                if step is None or rank is None or value < 0:
                    continue
                if group:
                    if stamp not in topology:
                        topology[stamp] = self.topology(scope, start, end, stamp)
                    if rank not in topology[stamp].get(group, {}).get("ranks", ()):
                        continue
                name = labels["scope"] if mode == "coverage" else labels.get("range_name", "")
                if not name or (names and name not in names):
                    continue
                dimension = (
                    labels.get("device", "") if mode == "coverage" else (labels.get("source"), labels.get("domain"))
                )
                newer(combined, (step, rank, name, dimension, metric), stamp, value)

        totals = defaultdict(lambda: [0.0, 0.0, set()])
        if mode == "coverage":
            per_rank = defaultdict(list)
            for (step, rank, name, _, _), (_, value) in combined.items():
                per_rank[step, rank, name].append(value)
            for (step, rank, name), values in per_rank.items():
                slot = totals[step, name]
                slot[0] += sum(values) / len(values)
                slot[1] += 1
                slot[2].add(rank)
        else:
            for (step, rank, name, dimension, metric), (stamp, calls) in combined.items():
                duration = combined.get((step, rank, name, dimension, RANGE_SUM))
                if metric != RANGE_CALLS or calls <= 0 or duration is None or duration[0] != stamp:
                    continue
                slot = totals[step, name]
                slot[0] += duration[1] * 1000
                slot[1] += calls
                slot[2].add(rank)

        result = defaultdict(dict)
        for (step, name), (total, divisor, valid_ranks) in totals.items():
            result[step][name] = {"value": total / divisor, "ranks": len(valid_ranks), "count": divisor}
        observed_steps = set(samples.values()) if not steps else set(steps)
        return {
            "names": sorted(available),
            "rows": [{"step": step, "values": result[step]} for step in sorted(observed_steps)],
        }

    def step_bounds(self, scope, start, end, step):
        buckets = self.overview(scope, start, end)
        bucket = buckets.get(step)
        return (bucket["_from"], bucket["_to"]) if bucket else None

    @lru_cache(maxsize=16)
    def _operators(self, key, start, end, step, kind, version):
        scope = dict(key)
        bounds = self.step_bounds(scope, start, end, step)
        if bounds is None:
            return (), {}
        rows = list(
            self.prom.rows(
                [OP_STEP, OP_TOTAL],
                {
                    **scope,
                    "operator_kind": kind,
                    "operator_scope": "stream",
                    "rank!=": "all",
                },
                *bounds,
            )
        )
        operators = {}
        observed = set()
        for reported_step, labels, stamp, duration in aligned_operator_durations(rows):
            name = labels.get("operator_type", "")
            if reported_step == step and name:
                observed.add(name)
                newer(operators, frozenset(labels.items()), stamp, duration)
        return tuple(sorted(observed)), operators

    def operators(self, scope, start, end, step, kind):
        return self._operators(tuple(sorted(scope.items())), start, end, step, kind, floor(time() / 10))

    def operator_trend_options(self, scope, start, end, rank):
        rank_filter = {"rank": rank} if rank else {"rank=~": "[0-9]+"}
        filters = {**scope, **rank_filter, "operator_scope": "stream"}
        operators = {}
        for kind in OPERATOR_KINDS:
            labels = {**filters, "operator_kind": kind}
            operators[kind] = sorted(filter(None, self.prom.labels(OP_TOTAL, "operator_type", labels, start, end)))
        result = {"operators": operators}
        if not rank:
            labels = {**filters, "operator_kind=~": "|".join(OPERATOR_KINDS)}
            ranks = self.prom.labels(OP_TOTAL, "rank", labels, start, end)
            result["ranks"] = sorted(ranks, key=lambda value: (int(value), value))
        return result

    def operator_trends(self, scope, start, end, kind, rank, operators):
        rank_filter = {"rank": rank} if rank else {"rank=~": "[0-9]+"}
        type_filter = (
            {"operator_type": operators[0]}
            if len(operators) == 1
            else {"operator_type=~": "|".join(map(escape, operators))}
        )
        filters = {**scope, **rank_filter, **type_filter, "operator_kind": kind, "operator_scope": "stream"}
        samples = {operator: defaultdict(dict) for operator in operators}
        for window in self.prom.windows([OP_STEP, OP_TOTAL], filters, start, end, width=DAY):
            for step, labels, stamp, duration in aligned_operator_durations(window):
                operator = labels.get("operator_type")
                if operator in samples:
                    newer(samples[operator][step], frozenset(labels.items()), stamp, duration)
        result = {operator: [] for operator in operators}
        for operator, steps in samples.items():
            for step, entries in sorted(steps.items()):
                totals = rank_totals(operator_samples(entries, operator))
                result[operator].append({"step": step, "ms": median(totals.values()), "count": len(totals)})
        return result

    def communication_rank_totals(self, scope, start, end, step, operator=""):
        _, operators = self.operators(scope, start, end, step, "Communication")
        return rank_totals(operator_samples(operators, operator))

    def domains(self, scope, start, end, step, kind, operator=""):
        bounds = self.step_bounds(scope, start, end, step)
        if bounds is None:
            return [], "", []
        topology = self.topology(scope, start, end, bounds[1])
        groups = {group: data for group, data in topology.items() if data["type"] in DOMAIN_TYPES}
        categories, operators = self.operators(scope, start, end, step, kind)
        matched = operator_samples(operators, operator)
        totals = defaultdict(Counter)
        attributes = defaultdict(lambda: defaultdict(set))
        unassigned = 0
        if kind == "Communication":
            for labels, duration in matched:
                group, rank = labels.get("comm_group"), rank_id(labels)
                if group not in topology or rank not in topology[group]["ranks"]:
                    unassigned += 1
                    continue
                if group not in groups:
                    continue
                totals[group][rank] += duration
                attributes[group][rank].add((labels.get("role", ""), labels.get("workload", "")))
        else:
            per_rank = rank_totals(matched)
            for group, data in groups.items():
                totals[group].update({rank: per_rank[rank] for rank in data["ranks"] if rank in per_rank})

        status = "域归属数据不足" if unassigned else ("无并行域成员数据" if not groups else "")
        rows = []
        for group, values in totals.items():
            if not values:
                continue
            cohort = (groups[group]["type"], len(groups[group]["ranks"]), len(values))
            comparable = True
            if kind == "Communication":
                signatures = {tuple(sorted(parts)) for parts in attributes[group].values()}
                comparable = len(signatures) == 1 and all(
                    role and work for signature in signatures for role, work in signature
                )
                cohort = (*cohort, next(iter(signatures))) if comparable else None
            mid = median(values.values())
            slow, slow_ms = min(values.items(), key=lambda item: (-item[1], item[0]))
            fast, fast_ms = min(values.items(), key=lambda item: (item[1], item[0]))
            rows.append(
                {
                    "group": group,
                    "type": groups[group]["type"],
                    "count": len(values),
                    "median": mid if comparable else None,
                    "fast": fast if comparable else None,
                    "fastMs": fast_ms if comparable else None,
                    "slow": slow if comparable else None,
                    "slowMs": slow_ms if comparable else None,
                    "positive": slow_ms - mid if comparable else None,
                    "negative": mid - fast_ms if comparable and kind == "Communication" else None,
                    "cohort": cohort,
                    "values": values,
                }
            )

        if not status and any(row["cohort"] is None for row in rows):
            status = "无可比基线"

        baselines = comparable_baselines(rows)
        for row in rows:
            base = baselines.get(row["group"])
            row["baseline"] = base
            row["offset"] = row["median"] - base if base is not None else None
            row["rate"] = row["offset"] / base if base else None
            row["tail"] = max(row["positive"], row["negative"] or 0) if row["positive"] is not None else None
        return rows, status, categories

    def kde_options(self, scope, start, end, step, kind, rank=None):
        bounds = self.step_bounds(scope, start, end, step)
        rank_filter = {"rank!=": "all"} if rank is None else {"rank": str(rank)}
        filters = {**scope, **rank_filter, "operator_kind": kind, "operator_scope": "stream"}
        rows = self.prom.rows([OP_STEP], filters, *bounds) if bounds else ()
        selected = (labels for _, labels, _, value in rows if value == step and labels.get("operator_type"))
        if rank is None:
            ranks = {labels["rank"] for labels in selected if rank_id(labels) is not None}
            return {"ranks": sorted(ranks, key=lambda rank: (int(rank), rank))}

        choices = sorted(
            {(labels.get("device", ""), labels.get("stream", ""), labels["operator_type"]) for labels in selected}
        )
        return {
            "operators": sorted({operator for _, _, operator in choices}),
            "choices": [
                {"device": device, "stream": stream, "operator": operator} for device, stream, operator in choices
            ],
        }

    def kde(self, scope, start, end, step, kind, rank, name):
        bounds = self.step_bounds(scope, start, end, step)
        if bounds is None:
            return []
        filters = {**scope, "rank": str(rank), "operator_kind": kind, "operator_scope": "stream", "operator_type": name}
        metrics = [OP_STEP, OP_CALLS, CLUSTER + "count", *(CLUSTER + suffix for suffix in CLUSTER_FIELDS)]
        rows = list(self.prom.rows(metrics, filters, *bounds))
        stamps = {}
        for metric, labels, stamp, value in rows:
            if metric == OP_STEP and value == step:
                newer(stamps, frozenset(labels.items()), stamp, True)
        results = defaultdict(lambda: {"clusters": defaultdict(dict)})
        for metric, labels, stamp, value in rows:
            if metric == OP_STEP:
                continue
            labels = dict(labels)
            cluster = labels.pop("cluster", None)
            key = frozenset(labels.items())
            if stamps.get(key, (None,))[0] != stamp:
                continue
            result = results[key]
            if metric == OP_CALLS:
                result["calls"] = int(value)
            elif metric == CLUSTER + "count":
                result["count"] = int(value)
            elif cluster is not None:
                field = CLUSTER_FIELDS[metric.removeprefix(CLUSTER)]
                result["clusters"][cluster][field] = int(value) if field == "calls" else value * 1000
        profiles = []
        for key, result in results.items():
            labels = dict(key)
            calls = result.get("calls")
            clusters = [
                {"id": cluster, "share": row["calls"] / calls if calls else None, **row}
                for cluster, row in result["clusters"].items()
                if set(row) == set(CLUSTER_FIELDS.values())
            ]
            if clusters:
                profiles.append(
                    {
                        "device": labels.get("device", ""),
                        "stream": labels.get("stream", ""),
                        "name": labels.get("operator_name", ""),
                        "execution": labels.get("execution_type", ""),
                        "calls": calls,
                        "count": result.get("count"),
                        "clusters": sorted(clusters, key=lambda row: (row["p50"], row["id"])),
                    }
                )
        return sorted(profiles, key=lambda row: (row["device"], row["stream"], row["name"], row["execution"]))
