// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include "PrometheusRemoteWrite.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <chrono>

#include <curl/curl.h>
#include <snappy.h>

namespace msmonitor::process {
namespace {
struct TransportConstants final {
    static constexpr std::size_t SeriesPerRequest{256};
    static constexpr std::size_t Attempts{3};
    static constexpr long TimeoutSeconds{15};
};

void Varint(std::string &out, std::uint64_t value) {
    while (value >= 128) {
        out.push_back(static_cast<char>((value & 127) | 128));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

void Bytes(std::string &out, std::uint64_t field, const std::string &value) {
    Varint(out, (field << 3) | 2);
    Varint(out, value.size());
    out += value;
}

std::size_t Response(char *, std::size_t size, std::size_t count, void *) { return size * count; }

void Post(const std::string &url, const std::string &token, const std::string &caFile, const std::string &request) {
    std::string compressed;
    snappy::Compress(request.data(), request.size(), &compressed);
    using Curl = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
    Curl handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle) {
        throw std::runtime_error("curl initialization failed");
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
    for (const auto *header : {"Content-Type: application/x-protobuf", "Content-Encoding: snappy",
             "X-Prometheus-Remote-Write-Version: 0.1.0", "User-Agent: msmonitor-process/1.0"}) {
        auto *next = curl_slist_append(headers.get(), header);
        if (!next) {
            throw std::bad_alloc();
        }
        headers.release();
        headers.reset(next);
    }
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    if (!token.empty()) {
        curl_easy_setopt(handle.get(), CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
        curl_easy_setopt(handle.get(), CURLOPT_XOAUTH2_BEARER, token.c_str());
    }
    if (!caFile.empty()) {
        curl_easy_setopt(handle.get(), CURLOPT_CAINFO, caFile.c_str());
    }
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, compressed.data());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(compressed.size()));
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, TransportConstants::TimeoutSeconds);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, Response);
    for (std::size_t attempt = 0; attempt < TransportConstants::Attempts; ++attempt) {
        const auto result = curl_easy_perform(handle.get());
        long status = 0;
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
        if (result == CURLE_OK && status >= 200 && status < 300) {
            return;
        }
        if ((result == CURLE_OK && status != 429 && status < 500) || attempt + 1 == TransportConstants::Attempts) {
            throw std::runtime_error(
                "Remote Write failed: HTTP " + std::to_string(status) + ", " + curl_easy_strerror(result));
        }
        std::this_thread::sleep_for(std::chrono::seconds(attempt + 1));
    }
}
} // namespace

PrometheusRemoteWrite::PrometheusRemoteWrite(std::string url, std::string token, std::string caFile)
    : url_(std::move(url)), token_(std::move(token)), caFile_(std::move(caFile)) {
    if (url_.rfind("http://", 0) != 0 && url_.rfind("https://", 0) != 0) {
        throw std::invalid_argument("Remote Write requires an HTTP(S) URL");
    }
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) {
        throw std::runtime_error("curl global initialization failed");
    }
}

void PrometheusRemoteWrite::Write(const std::vector<MetricSample> &samples) const {
    std::map<std::map<std::string, std::string>, std::map<std::int64_t, double>> series;
    for (const auto &sample : samples) {
        if (!std::isfinite(sample.value)) {
            throw std::invalid_argument("non-finite metric sample");
        }
        auto labels = sample.labels;
        labels["__name__"] = sample.name;
        const auto [position, inserted] = series[labels].emplace(sample.timestampMs, sample.value);
        if (!inserted && position->second != sample.value) {
            throw std::runtime_error("conflicting samples at the same millisecond");
        }
    }
    std::string request;
    std::size_t batchSize = 0;
    for (const auto &[labels, values] : series) {
        std::string timeSeries;
        for (const auto &[name, value] : labels) {
            std::string label;
            Bytes(label, 1, name);
            Bytes(label, 2, value);
            Bytes(timeSeries, 1, label);
        }
        for (const auto &[timestamp, value] : values) {
            std::string point;
            Varint(point, 9);
            std::uint64_t bits;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            for (unsigned shift = 0; shift < 64; shift += 8) {
                point.push_back(static_cast<char>((bits >> shift) & 255));
            }
            Varint(point, 16);
            Varint(point, static_cast<std::uint64_t>(timestamp));
            Bytes(timeSeries, 2, point);
        }
        Bytes(request, 1, timeSeries);
        if (++batchSize == TransportConstants::SeriesPerRequest) {
            Post(url_, token_, caFile_, request);
            request.clear();
            batchSize = 0;
        }
    }
    if (!request.empty()) {
        Post(url_, token_, caFile_, request);
    }
}
} // namespace msmonitor::process
