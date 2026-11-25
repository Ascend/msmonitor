import unittest
from collections import Counter
from unittest.mock import Mock, patch

from server import dispatch, rank_result, table
from prometheus import DAY
from summary import (
    DURATION,
    OP_STEP,
    OP_TOTAL,
    STEP,
    VALID,
    Summary,
    add_rank,
    comparable_baselines,
    durations,
    fresh_bucket,
    newer,
    step_keys,
    summarize_reporting,
)


class StepStatisticsTest(unittest.TestCase):
    def test_duplicate_and_all_are_excluded_from_rank_count(self):
        rows = (
            [
                (STEP, {"rank": rank}, stamp, 12.0)
                for rank, stamp in (("0", 1), ("0", 2), ("1", 1), ("2", 1), ("all", 1))
            ]
            + [
                (DURATION, {"rank": rank}, stamp, value)
                for rank, stamp, value in (
                    ("0", 1, 0.09),
                    ("0", 2, 0.10),
                    ("1", 1, 0.14),
                    ("2", 1, 0.8),
                    ("all", 1, 10.0),
                )
            ]
            + [(VALID, {"rank": "2"}, 1, 0.0)]
        )
        latest = {}
        for _, rank, stamp, duration in durations(rows, step_keys(rows)):
            newer(latest, rank, stamp, duration)
        bucket = {**fresh_bucket(), "step": 12}
        for rank, (_, duration) in latest.items():
            add_rank(bucket, rank, duration)
        result = summarize_reporting(bucket, 2, 1, 30, 40)
        self.assertEqual((result["count"], result["mean"], result["spread"]), (2, 120, 40))

    def test_one_missing_among_one_hundred_thousand(self):
        bucket = {**fresh_bucket(), "step": 12}
        for rank in range(99_999):
            add_rank(bucket, rank, 100)
        pending = summarize_reporting(bucket, 100_000, 10, 30, 39)
        self.assertEqual((pending["status"], pending["missing"]), ("上报中", None))
        self.assertEqual(summarize_reporting(bucket, 100_000, 10, 30, 40)["missing"], 1)
        add_rank(bucket, 99_999, 100)
        self.assertEqual(summarize_reporting(bucket, 100_000, 10, 30, 45)["missing"], 0)

    def test_spread_requires_two_valid_ranks(self):
        bucket = {**fresh_bucket(), "step": 12}
        self.assertIsNone(summarize_reporting(bucket, None, None, None, 0)["mean"])
        add_rank(bucket, 7, 120)
        self.assertIsNone(summarize_reporting(bucket, None, None, None, 0)["spread"])


class DomainStatisticsTest(unittest.TestCase):
    def test_peer_baseline_is_leave_one_out_and_within_type(self):
        rows = [{"group": f"tp-{value}", "median": value, "cohort": ("tp", 8, 8)} for value in (80, 100, 120, 140)]
        rows.append({"group": "dp-200", "median": 200, "cohort": ("dp", 8, 8)})
        bases = comparable_baselines(rows)
        self.assertEqual(bases["tp-140"], 100)
        self.assertNotIn("dp-200", bases)

    def test_rank_result_keeps_both_signs_when_requested(self):
        rows = [
            {
                "group": "tp [0,1,2]",
                "count": 3,
                "median": 100,
                "baseline": None,
                "values": Counter({0: 20, 1: 100, 2: 110}),
            }
        ]
        for kind in ("Kernel", "Communication"):
            with self.subTest(kind=kind):
                result = rank_result(rows, "tp [0,1,2]", "local", kind, "both", 10, 5)
                self.assertEqual([row["offset"] for row in result["rows"]], [-80, 10, 0])
                self.assertEqual((result["count"], result["positive"], result["negative"]), (3, 1, 1))
        default = rank_result(rows, "tp [0,1,2]", "local", "Kernel", "", 10, 5)
        self.assertEqual([row["offset"] for row in default["rows"]], [10])

    def test_tables_sort_before_pagination(self):
        rows = [
            {
                "group": str(rank),
                "type": "tp",
                "count": 1,
                "median": 10,
                "baseline": 10,
                "offset": rank,
                "rate": rank / 10,
            }
            for rank in range(41)
        ]
        result = table(rows, "offset", "tp", "", "offset", True, 2)
        self.assertEqual(result["total"], 41)
        self.assertEqual([row["offset"] for row in result["rows"]], list(range(20, 0, -1)))


class CommunicationRankStatisticsTest(unittest.TestCase):
    def test_step_rank_totals_and_median_without_domain_labels(self):
        prometheus = Mock()
        samples = []
        for rank, stream, stamp, step, duration in (
            ("0", "40", 10, 12, 0.03),
            ("0", "40", 11, 12, 0.02),
            ("0", "41", 11, 12, 0.09),
            ("1", "40", 11, 12, 0.02),
            ("2", "40", 11, 12, 0.1),
            ("3", "40", 12, 13, 0.8),
            ("all", "40", 11, 12, 0.9),
        ):
            operator = "hcom_allGather_" if stream == "41" else "hcom_allReduce_"
            labels = {"rank": rank, "stream": stream, "operator_type": operator}
            samples.extend(((OP_STEP, labels, stamp, step), (OP_TOTAL, labels, stamp, duration)))
        prometheus.rows.return_value = samples
        summary = Summary(prometheus)
        params = {"job": ["job"], "run": ["run"], "from": ["5"], "to": ["25"], "step": ["12"]}
        with patch.object(summary, "step_bounds", return_value=(10, 20)):
            result = dispatch(summary, "communication-ranks", params)
            prometheus.rows.assert_called_once_with(
                [OP_STEP, OP_TOTAL],
                {
                    "job_name": "job",
                    "run_id": "run",
                    "operator_kind": "Communication",
                    "operator_scope": "stream",
                    "rank!=": "all",
                },
                10,
                20,
            )
            filtered = dispatch(summary, "communication-ranks", {**params, "operator": ["hcom_allGather_"]})
            missing = dispatch(summary, "communication-ranks", {**params, "operator": ["unavailable"]})

        self.assertEqual((result["count"], result["baseline"]), (3, 100))
        self.assertEqual(
            result["rows"],
            [
                {"rank": 1, "ms": 20, "offset": -80},
                {"rank": 0, "ms": 110, "offset": 10},
                {"rank": 2, "ms": 100, "offset": 0},
            ],
        )
        self.assertEqual(filtered, {"rows": [{"rank": 0, "ms": 90, "offset": 0}], "count": 1, "baseline": 90})
        self.assertEqual(missing, {"rows": [], "count": 0, "baseline": None})


class KdeOptionsTest(unittest.TestCase):
    def test_rank_and_filter_choices_use_exact_step_and_stream_labels(self):
        prometheus = Mock()
        samples = [
            (OP_STEP, {"rank": rank, "device": device, "stream": stream, "operator_type": operator}, stamp, step)
            for rank, device, stream, operator, stamp, step in (
                ("02", "1", "40", "allReduce", 10, 12),
                ("02", "1", "40", "allReduce", 11, 12),
                ("02", "2", "41", "allGather", 11, 12),
                ("02", "2", "42", "broadcast", 11, 13),
                ("10", "2", "42", "broadcast", 11, 12),
                ("all", "0", "40", "allReduce", 11, 12),
            )
        ]
        prometheus.rows.side_effect = lambda metrics, matchers, start, end: (
            row for row in samples if "rank" not in matchers or row[1]["rank"] == matchers["rank"]
        )
        summary = Summary(prometheus)
        params = {
            "job": ["job"],
            "run": ["run"],
            "from": ["5"],
            "to": ["25"],
            "step": ["12"],
            "kind": ["Communication"],
        }
        with patch.object(summary, "step_bounds", return_value=(10, 20)):
            self.assertEqual(dispatch(summary, "kde-options", params), {"ranks": ["02", "10"]})
            result = dispatch(summary, "kde-options", {**params, "rank": ["02"]})

        self.assertEqual(
            result,
            {
                "operators": ["allGather", "allReduce"],
                "choices": [
                    {"device": "1", "stream": "40", "operator": "allReduce"},
                    {"device": "2", "stream": "41", "operator": "allGather"},
                ],
            },
        )
        prometheus.rows.assert_called_with(
            [OP_STEP],
            {
                "job_name": "job",
                "run_id": "run",
                "operator_kind": "Communication",
                "operator_scope": "stream",
                "rank": "02",
            },
            10,
            20,
        )


class OperatorTrendTest(unittest.TestCase):
    def test_latest_stream_samples_are_summed_per_rank_before_taking_the_median(self):
        prometheus = Mock()

        def samples(rank, stream, stamp, step, duration, duration_stamp=None):
            labels = {"rank": rank, "stream": stream, "operator_type": "MatMulV2"}
            return [
                (OP_STEP, labels, stamp, step),
                (OP_TOTAL, labels, stamp if duration_stamp is None else duration_stamp, duration),
            ]

        windows = [
            samples("0", "1", 5, 12, 0.01)
            + samples("0", "2", 5, 12, 0.01)
            + samples("1", "1", 5, 12, 0.08)
            + samples("2", "1", 5, 12, 9, duration_stamp=6),
            samples("0", "1", 10, 12, 0.03) + samples("1", "1", 10, 13, 0) + samples("0", "1", 11, 14, -1),
        ]

        def query_windows(metrics, matchers, start, end, width):
            rank = matchers.get("rank")
            return ([row for row in window if rank is None or row[1]["rank"] == rank] for window in windows)

        prometheus.windows.side_effect = query_windows
        summary = Summary(prometheus)
        params = {
            "job": ["job"],
            "run": ["run"],
            "from": ["5"],
            "to": ["25"],
            "kind": ["Kernel"],
            "operator": ["MatMulV2"],
        }

        self.assertEqual(
            dispatch(summary, "operator-trend", params),
            {
                "rows": [
                    {"step": 12, "ms": 60, "count": 2},
                    {"step": 13, "ms": 0, "count": 1},
                ]
            },
        )
        prometheus.windows.assert_called_once_with(
            [OP_STEP, OP_TOTAL],
            {
                "job_name": "job",
                "run_id": "run",
                "operator_kind": "Kernel",
                "operator_scope": "stream",
                "rank=~": "[0-9]+",
                "operator_type": "MatMulV2",
            },
            5,
            25,
            width=DAY,
        )
        self.assertEqual(
            dispatch(summary, "operator-trend", {**params, "rank": ["0"]}),
            {
                "rows": [{"step": 12, "ms": 40, "count": 1}],
            },
        )

    def test_requested_types_return_independent_series(self):
        prometheus = Mock()
        samples = []
        for operator, rank, step, duration in (
            ("MatMulV2", "0", 12, 0.02),
            ("MatMulV2", "1", 12, 0.08),
            ("MatMulV(3)", "0", 12, 0.10),
            ("MatMulV(3)", "1", 13, 0.12),
            ("MatMulV3", "0", 12, 9),
        ):
            labels = {"operator_type": operator, "rank": rank, "stream": "1"}
            samples.extend(((OP_STEP, labels, 10 + step, step), (OP_TOTAL, labels, 10 + step, duration)))
        prometheus.windows.return_value = [samples]
        params = {
            "job": ["job"],
            "run": ["run"],
            "from": ["5"],
            "to": ["25"],
            "kind": ["Kernel"],
            "operators": ['["MatMulV2","MatMulV(3)","MatMulV2","Missing"]'],
        }

        self.assertEqual(
            dispatch(Summary(prometheus), "operator-trends", params),
            {
                "series": {
                    "MatMulV2": [{"step": 12, "ms": 50, "count": 2}],
                    "MatMulV(3)": [{"step": 12, "ms": 100, "count": 1}, {"step": 13, "ms": 120, "count": 1}],
                    "Missing": [],
                }
            },
        )
        prometheus.windows.assert_called_once_with(
            [OP_STEP, OP_TOTAL],
            {
                "job_name": "job",
                "run_id": "run",
                "operator_kind": "Kernel",
                "operator_scope": "stream",
                "rank=~": "[0-9]+",
                "operator_type=~": r"MatMulV2|MatMulV\(3\)|Missing",
            },
            5,
            25,
            width=DAY,
        )
