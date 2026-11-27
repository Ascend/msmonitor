// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace msmonitor::process
{
class RemoteWriteError final : public std::runtime_error
{
   public:
    using std::runtime_error::runtime_error;
};

struct MetricSample
{
    std::string name;
    std::map<std::string, std::string> labels;
    double value;
    std::int64_t timestampMs;
};

// Prometheus Remote Write 1.0: protobuf WriteRequest + Snappy block + HTTP POST.
// Transport is optional at build time; the metric core has no HTTP dependency.
class PrometheusRemoteWrite final
{
   public:
    explicit PrometheusRemoteWrite(std::string url, std::string token = {}, std::string caFile = {});
    ~PrometheusRemoteWrite();
    void Write(const std::vector<MetricSample> &samples);

   private:
    struct Connection;
    std::unique_ptr<Connection> connection_;
};
}  // namespace msmonitor::process
