#pragma once
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include "Query/IQuery.h"

namespace NASCP {
    namespace Providers {
        namespace Prometheus {

            class PrometheusQuery : public Query::IQuery {
            public:
                PrometheusQuery() = default;
                ~PrometheusQuery() override;

                double Fetch(const std::string& device, Query::DataType data, const std::string& customQuery = "") override;
                std::wstring GetString(const std::string& device, Query::DataType data, const std::string& customQuery = "") override;
                bool IsAvailable() override;
                void SetEndpoint(const std::string& endpoint, bool proxy = false) override;
                void SetDebug(void* rm, int debugLevel);

            private:
                std::string endpoint_;
                bool proxy_ = false;
                void* rm_ = nullptr;
                int debugLevel_ = 0;

                std::map<Query::DataType, double> cache_;
                std::wstring stringCache_;
                mutable std::mutex cacheMutex_;

                std::chrono::steady_clock::time_point lastFetch_;
                std::mutex fetchMutex_;

                std::thread workerThread_;
                std::atomic<bool> requestPending_{ false };
                std::atomic<bool> shutdown_{ false };

                void AsyncFetch(const std::string& device);

                std::string BuildRawQuery(const std::string& device) const;
                std::string HttpGet(const std::string& path) const;
                std::string UrlEncode(const std::string& value) const;

                void ParseRawResponse(const std::string& json, const std::string& device);

                double ParseSingleValue(const std::string& json) const;
                double GetCached(Query::DataType data) const;
                std::wstring FormatUptime(double seconds) const;
            };

        } // namespace Prometheus
    } // namespace Providers
} // namespace NASCP