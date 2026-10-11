#pragma once
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
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

                // Which Prometheus label Device= is matched against. node_exporter
                // always carries "job"; "device" and friends only exist if the
                // scrape config adds them.
                void SetDeviceLabel(const std::string& label);

                // Filesystem mountpoint the drive_* metrics describe.
                void SetMount(const std::string& mount);

                // chip=~ regex for the hwmon sensor cpu_temp reads. Prometheus regexes
                // are fully anchored, so a bare "coretemp.*" never matches a chip
                // actually named "platform_coretemp_0".
                void SetTempChip(const std::string& chip);

                // How often the background poller re-queries Prometheus, in ms.
                void SetPollInterval(int ms);

                // Stops the poller without waiting for it. Used on shutdown so the
                // pollers wind down in parallel rather than one join at a time.
                void Stop() override;

            private:
                // One network round trip each, issued only when some measure has
                // actually asked for a metric in that group.
                enum class Group {
                    Raw,          // up, memory, swap, boot time, load - one selector
                    CpuUsage,
                    CpuClock,
                    CpuTemp,
                    NetRates,     // receive + transmit per second
                    NetTotals,    // receive + transmit counters
                    NetSpeed,
                    DiskRates,    // read + written per second
                    DiskTotals,   // read + written counters
                    Filesystem,   // size + avail -> used/free/percent
                    Custom
                };
                static Group GroupOf(Query::DataType data);

                std::string endpoint_;
                bool proxy_ = false;
                void* rm_ = nullptr;
                int debugLevel_ = 0;
                std::string deviceLabel_ = "job";
                std::string mount_ = "/";
                std::string tempChip_ = ".*coretemp.*|.*k10temp.*|.*zenpower.*|.*pci.*";
                std::atomic<int> pollIntervalMs_{ 2000 };

                std::map<Query::DataType, double> cache_;
                std::wstring stringCache_;
                mutable std::mutex cacheMutex_;

                // What the skin has asked for, and for whom.
                std::set<Group> requested_;
                std::string device_;
                std::string customQuery_;
                std::mutex stateMutex_;

                std::thread poller_;
                std::atomic<bool> running_{ false };
                std::atomic<bool> started_{ false };
                // False until a gather completes without error; every read returns
                // -1 while it is false, so "cannot reach Prometheus" never looks
                // like a genuine zero.
                std::atomic<bool> healthy_{ false };

                void EnsurePoller();
                void PollLoop();
                void GatherOnce();

                std::string Selector(const std::string& device, const std::string& extra = "") const;
                std::string HttpGet(const std::string& path) const;
                std::string Query(const std::string& promql) const;
                std::string UrlEncode(const std::string& value) const;

                void ParseRawResponse(const std::string& json);
                std::map<std::string, double> ParseByMetricName(const std::string& json) const;

                double ParseSingleValue(const std::string& json) const;
                double GetCached(Query::DataType data) const;
                std::wstring FormatUptime(double seconds) const;

                void Store(Query::DataType data, double value);
            };

        } // namespace Prometheus
    } // namespace Providers
} // namespace NASCP
