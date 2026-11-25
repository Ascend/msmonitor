"""Read Prometheus samples at their stored timestamps, without lookback or resampling."""

import json
import math
import os
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import ProxyHandler, Request, build_opener

PREFIX = "msmonitor_"
HOUR = 3_600_000
DAY = 24 * HOUR


def selector(metrics, labels):
    matchers = dict(labels)
    if len(metrics) == 1:
        matchers["__name__"] = metrics[0]
    else:
        matchers["__name__=~"] = "|".join(metrics)
    return (
        "{"
        + ",".join(
            f'{name}={json.dumps(value, ensure_ascii=False)}'
            if not name.endswith(("=~", "!="))
            else f'{name}{json.dumps(value, ensure_ascii=False)}'
            for name, value in matchers.items()
        )
        + "}"
    )


class PrometheusError(Exception):
    pass


class Prometheus:
    def __init__(self):
        self.url = os.environ.get("MSMONITOR_PROMETHEUS_URL", "http://127.0.0.1:19094")
        self.opener = build_opener(ProxyHandler({}))

    def request(self, path, params):
        data = urlencode(params, doseq=True).encode()
        url = self.url.rstrip("/") + "/api/v1/" + path
        req = Request(url, data=data) if path == "query" else Request(url + "?" + data.decode())
        try:
            with self.opener.open(req, timeout=25) as response:
                return json.load(response)["data"]
        except HTTPError as error:
            try:
                detail = json.load(error).get("error", str(error))
            except ValueError:
                detail = str(error)
            raise PrometheusError(detail) from error
        except (URLError, TimeoutError) as error:
            raise PrometheusError("无法连接 Prometheus") from error

    def labels(self, metric, name, labels, start, end):
        return self.request(
            f"label/{quote(name)}/values",
            {
                "match[]": selector([metric], labels),
                "start": start / 1000,
                "end": end / 1000,
            },
        )

    def vector(self, expression, end):
        return self.request("query", {"query": expression, "time": end / 1000})["result"]

    def rows(self, metrics, labels, start, end):
        if end < start or not metrics:
            return
        expression = selector(metrics, labels) + f"[{end - start + 1}ms]"
        try:
            rows = self.vector(expression, end)
        except PrometheusError as error:
            if end - start < 1000 or "too many samples" not in str(error):
                raise
            middle = (start + end) // 2
            yield from self.rows(metrics, labels, start, middle)
            yield from self.rows(metrics, labels, middle + 1, end)
            return
        for row in rows:
            labels = dict(row["metric"])
            metric = labels.pop("__name__")
            for timestamp, value in row["values"]:
                number = float(value)
                if math.isfinite(number):
                    yield metric, labels, round(timestamp * 1000), number

    def windows(self, metrics, labels, start, end, width=HOUR):
        for since in range(start, end + 1, width):
            yield list(self.rows(metrics, labels, since, min(since + width - 1, end)))
