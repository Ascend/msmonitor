// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace msmonitor::process {
struct MetricSample {
    std::string name;
    std::map<std::string, std::string> labels;
    double value;
    std::int64_t timestampMs;
};

// Prometheus Remote Write 1.0: protobuf WriteRequest + Snappy block + HTTP POST.
// Transport is optional at build time; the metric core has no HTTP dependency.
class PrometheusRemoteWrite final {
  public:
    explicit PrometheusRemoteWrite(std::string url, std::string token = {}, std::string caFile = {});
    void Write(const std::vector<MetricSample> &samples) const;

  private:
    std::string url_;
    std::string token_;
    std::string caFile_;
};
} // namespace msmonitor::process
