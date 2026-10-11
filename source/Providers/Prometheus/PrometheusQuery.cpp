#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include "PrometheusQuery.h"
#include "Utils/Debug.h"
#include "Utils/StringUtils.h"
#include <sstream>
#include <stdexcept>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")

namespace NASCP {
    namespace Providers {
        namespace Prometheus {

            namespace {
                // Returned for anything that is not a real reading: endpoint
                // unreachable, HTTP error, or a metric this exporter does not
                // publish. Zero would be indistinguishable from a true zero.
                constexpr double kNoData = -1.0;

                constexpr int kHttpTimeoutMs = 3000;
            }

            PrometheusQuery::~PrometheusQuery() {
                running_ = false;
                if (poller_.joinable())
                    poller_.join();
            }

            void PrometheusQuery::Stop() {
                running_ = false;
            }

            void PrometheusQuery::SetEndpoint(const std::string& endpoint, bool proxy) {
                endpoint_ = endpoint;
                proxy_ = proxy;
            }

            void PrometheusQuery::SetDebug(void* rm, int debugLevel) {
                rm_ = rm;
                debugLevel_ = debugLevel;
            }

            void PrometheusQuery::SetDeviceLabel(const std::string& label) {
                if (!label.empty())
                    deviceLabel_ = label;
            }

            void PrometheusQuery::SetMount(const std::string& mount) {
                if (!mount.empty())
                    mount_ = mount;
            }

            void PrometheusQuery::SetTempChip(const std::string& chip) {
                if (!chip.empty())
                    tempChip_ = chip;
            }

            void PrometheusQuery::SetPollInterval(int ms) {
                if (ms >= 250)
                    pollIntervalMs_ = ms;
            }

            PrometheusQuery::Group PrometheusQuery::GroupOf(Query::DataType data) {
                switch (data) {
                case Query::DataType::CPU_Usage:         return Group::CpuUsage;
                case Query::DataType::CPU_Clock:         return Group::CpuClock;
                case Query::DataType::CPU_Temp:          return Group::CpuTemp;

                case Query::DataType::Net_Download:
                case Query::DataType::Net_Upload:        return Group::NetRates;
                case Query::DataType::Net_DownloadTotal:
                case Query::DataType::Net_UploadTotal:   return Group::NetTotals;
                case Query::DataType::Net_Speed:         return Group::NetSpeed;

                case Query::DataType::Drive_ReadSpeed:
                case Query::DataType::Drive_WriteSpeed:  return Group::DiskRates;
                case Query::DataType::Drive_Read:
                case Query::DataType::Drive_Write:       return Group::DiskTotals;
                case Query::DataType::Drive_UsedPercent:
                case Query::DataType::Drive_Used:
                case Query::DataType::Drive_Free:
                case Query::DataType::Drive_Total:       return Group::Filesystem;

                case Query::DataType::Custom:            return Group::Custom;

                default:                                 return Group::Raw;
                }
            }

            bool PrometheusQuery::IsAvailable() {
                return healthy_;
            }

            double PrometheusQuery::Fetch(const std::string& device, Query::DataType data, const std::string& customQuery) {
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    device_ = device;
                    requested_.insert(GroupOf(data));
                    if (data == Query::DataType::Custom && !customQuery.empty())
                        customQuery_ = customQuery;
                }

                EnsurePoller();
                return GetCached(data);
            }

            void PrometheusQuery::EnsurePoller() {
                bool expected = false;
                if (!started_.compare_exchange_strong(expected, true))
                    return;

                running_ = true;
                poller_ = std::thread(&PrometheusQuery::PollLoop, this);
            }

            void PrometheusQuery::PollLoop() {
                // Gather immediately so the first skin tick has data soon, then
                // on the configured interval. The wait is sliced so shutdown does
                // not have to sit through a whole interval.
                while (running_) {
                    GatherOnce();

                    const int interval = pollIntervalMs_;
                    for (int waited = 0; waited < interval && running_; waited += 50)
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            std::string PrometheusQuery::Selector(const std::string& device, const std::string& extra) const {
                std::string s = deviceLabel_ + "=\"" + device + "\"";
                if (!extra.empty())
                    s += "," + extra;
                return s;
            }

            void PrometheusQuery::GatherOnce() {
                std::string device;
                std::set<Group> groups;
                std::string customQuery;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    device = device_;
                    groups = requested_;
                    customQuery = customQuery_;
                }

                if (device.empty() && groups.count(Group::Custom) == 0) {
                    DEBUG_ERROR(rm_, debugLevel_, L"No Device= set, nothing to query");
                    healthy_ = false;
                    return;
                }

                // node_exporter labels NICs, disks and filesystems "device". When
                // the scrape config adds its own "device" target label, Prometheus
                // keeps the target's and renames the metric's to "exported_device",
                // so the interface filter has to follow it or it excludes nothing.
                const std::string ifLabel = (deviceLabel_ == "device") ? "exported_device" : "device";
                const std::string ifFilter = ifLabel + "!~\"lo|docker.*|veth.*|br.*|virbr.*\"";

                try {
                    if (groups.count(Group::Custom) && !customQuery.empty())
                        Store(Query::DataType::Custom, ParseSingleValue(Query(customQuery)));

                    if (groups.count(Group::Raw)) {
                        ParseRawResponse(Query(
                            "{" + Selector(device) + ",__name__=~\"up|node_memory_MemAvailable_bytes"
                            "|node_memory_MemTotal_bytes|node_memory_SwapTotal_bytes|node_memory_SwapFree_bytes"
                            "|node_boot_time_seconds|node_load1\"}"));
                    }

                    if (groups.count(Group::CpuUsage)) {
                        Store(Query::DataType::CPU_Usage, ParseSingleValue(Query(
                            "100 - avg by (" + deviceLabel_ + ") (rate(node_cpu_seconds_total{"
                            + Selector(device, "mode=\"idle\"") + "}[1m])) * 100")));
                    }

                    if (groups.count(Group::CpuClock)) {
                        Store(Query::DataType::CPU_Clock, ParseSingleValue(Query(
                            "avg by (" + deviceLabel_ + ") (node_cpu_scaling_frequency_hertz{" + Selector(device) + "})")));
                    }

                    if (groups.count(Group::CpuTemp)) {
                        Store(Query::DataType::CPU_Temp, ParseSingleValue(Query(
                            "avg(node_hwmon_temp_celsius{" + Selector(device, "chip=~\"" + tempChip_ + "\",sensor=\"temp1\"") + "})")));
                    }

                    // rate() drops __name__, so receive and transmit cannot be told
                    // apart in a batched response - these two stay separate.
                    if (groups.count(Group::NetRates)) {
                        Store(Query::DataType::Net_Download, ParseSingleValue(Query(
                            "sum (rate(node_network_receive_bytes_total{" + Selector(device, ifFilter) + "}[1m]))")));
                        Store(Query::DataType::Net_Upload, ParseSingleValue(Query(
                            "sum (rate(node_network_transmit_bytes_total{" + Selector(device, ifFilter) + "}[1m]))")));
                    }

                    if (groups.count(Group::NetTotals)) {
                        auto v = ParseByMetricName(Query(
                            "sum by (__name__) ({" + Selector(device, "__name__=~\"node_network_receive_bytes_total"
                            "|node_network_transmit_bytes_total\"," + ifFilter) + "})"));
                        Store(Query::DataType::Net_DownloadTotal, v.count("node_network_receive_bytes_total") ? v["node_network_receive_bytes_total"] : kNoData);
                        Store(Query::DataType::Net_UploadTotal, v.count("node_network_transmit_bytes_total") ? v["node_network_transmit_bytes_total"] : kNoData);
                    }

                    if (groups.count(Group::NetSpeed)) {
                        Store(Query::DataType::Net_Speed, ParseSingleValue(Query(
                            "max by (" + deviceLabel_ + ") (node_network_speed_bytes{" + Selector(device, ifFilter) + "})")));
                    }

                    // Same reason as the network rates above.
                    if (groups.count(Group::DiskRates)) {
                        Store(Query::DataType::Drive_ReadSpeed, ParseSingleValue(Query(
                            "sum (rate(node_disk_read_bytes_total{" + Selector(device) + "}[1m]))")));
                        Store(Query::DataType::Drive_WriteSpeed, ParseSingleValue(Query(
                            "sum (rate(node_disk_written_bytes_total{" + Selector(device) + "}[1m]))")));
                    }

                    if (groups.count(Group::DiskTotals)) {
                        auto v = ParseByMetricName(Query(
                            "sum by (__name__) ({" + Selector(device, "__name__=~\"node_disk_read_bytes_total"
                            "|node_disk_written_bytes_total\"") + "})"));
                        Store(Query::DataType::Drive_Read, v.count("node_disk_read_bytes_total") ? v["node_disk_read_bytes_total"] : kNoData);
                        Store(Query::DataType::Drive_Write, v.count("node_disk_written_bytes_total") ? v["node_disk_written_bytes_total"] : kNoData);
                    }

                    // size and avail in one response; used, free and percent are
                    // arithmetic on those two.
                    if (groups.count(Group::Filesystem)) {
                        auto v = ParseByMetricName(Query(
                            "{" + Selector(device, "__name__=~\"node_filesystem_size_bytes|node_filesystem_avail_bytes\","
                            "mountpoint=\"" + mount_ + "\",fstype!~\"tmpfs|overlay\"") + "}"));

                        const bool have = v.count("node_filesystem_size_bytes") && v.count("node_filesystem_avail_bytes");
                        const double size = have ? v["node_filesystem_size_bytes"] : kNoData;
                        const double avail = have ? v["node_filesystem_avail_bytes"] : kNoData;

                        Store(Query::DataType::Drive_Total, size);
                        Store(Query::DataType::Drive_Free, avail);
                        Store(Query::DataType::Drive_Used, have ? size - avail : kNoData);
                        Store(Query::DataType::Drive_UsedPercent, (have && size > 0) ? ((size - avail) / size) * 100.0 : kNoData);
                    }

                    healthy_ = true;
                }
                catch (const std::exception& e) {
                    healthy_ = false;
                    DEBUG_ERROR(rm_, debugLevel_, (L"Gather failed: " + Str::ToWide(e.what())).c_str());
                }
                catch (...) {
                    healthy_ = false;
                    DEBUG_ERROR(rm_, debugLevel_, L"Gather failed: unknown error");
                }

                // Uptime is cached as boot time; the printable form is derived here
                // so GetString does no work on Rainmeter's thread.
                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    auto it = cache_.find(Query::DataType::Uptime);
                    if (it != cache_.end() && it->second > 0.0) {
                        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        stringCache_ = FormatUptime(static_cast<double>(now) - it->second);
                    }
                }
            }

            void PrometheusQuery::Store(Query::DataType data, double value) {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                cache_[data] = value;
            }

            std::string PrometheusQuery::Query(const std::string& promql) const {
                return HttpGet("/api/v1/query?query=" + UrlEncode(promql));
            }

            void PrometheusQuery::ParseRawResponse(const std::string& json) {
                std::lock_guard<std::mutex> lock(cacheMutex_);

                size_t pos = 0;
                while ((pos = json.find("\"__name__\":\"", pos)) != std::string::npos) {
                    pos += 12;
                    auto end = json.find('"', pos);
                    if (end == std::string::npos) break;

                    std::string name = json.substr(pos, end - pos);

                    auto valPos = json.find("\"value\":[", end);
                    if (valPos == std::string::npos) break;

                    valPos = json.find(',', valPos);
                    if (valPos == std::string::npos) break;
                    valPos++;

                    while (valPos < json.size() && (json[valPos] == ' ' || json[valPos] == '"')) valPos++;
                    auto valEnd = json.find('"', valPos);
                    if (valEnd == std::string::npos) break;

                    double value = 0.0;
                    try { value = std::stod(json.substr(valPos, valEnd - valPos)); }
                    catch (...) {}

                    if (name == "up")
                        cache_[Query::DataType::Status] = value;
                    else if (name == "node_memory_MemAvailable_bytes") {
                        cache_[Query::DataType::RAM_Free] = value;
                        // Results arrive in whatever order Prometheus sends them,
                        // so recompute here as well as in the MemTotal branch.
                        double total = cache_.count(Query::DataType::RAM_Total) ? cache_[Query::DataType::RAM_Total] : 0.0;
                        if (total > 0) {
                            double used = total - value;
                            cache_[Query::DataType::RAM_Used] = used;
                            cache_[Query::DataType::RAM_Percent] = (used / total) * 100.0;
                        }
                    }
                    else if (name == "node_memory_MemTotal_bytes") {
                        cache_[Query::DataType::RAM_Total] = value;
                        double free = cache_.count(Query::DataType::RAM_Free) ? cache_[Query::DataType::RAM_Free] : 0.0;
                        double used = value - free;
                        cache_[Query::DataType::RAM_Used] = used;
                        if (value > 0)
                            cache_[Query::DataType::RAM_Percent] = (used / value) * 100.0;
                    }
                    else if (name == "node_memory_SwapTotal_bytes") {
                        cache_[Query::DataType::Swap_Total] = value;
                        double free = cache_.count(Query::DataType::Swap_Free) ? cache_[Query::DataType::Swap_Free] : 0.0;
                        double used = value - free;
                        cache_[Query::DataType::Swap_Used] = used;
                        if (value > 0)
                            cache_[Query::DataType::Swap_Percent] = (used / value) * 100.0;
                    }
                    else if (name == "node_memory_SwapFree_bytes") {
                        cache_[Query::DataType::Swap_Free] = value;
                        double total = cache_.count(Query::DataType::Swap_Total) ? cache_[Query::DataType::Swap_Total] : 0.0;
                        if (total > 0) {
                            double used = total - value;
                            cache_[Query::DataType::Swap_Used] = used;
                            cache_[Query::DataType::Swap_Percent] = (used / total) * 100.0;
                        }
                    }
                    else if (name == "node_boot_time_seconds")
                        cache_[Query::DataType::Uptime] = value;
                    else if (name == "node_load1")
                        cache_[Query::DataType::Load] = value;

                    pos = valEnd;
                }
            }

            std::map<std::string, double> PrometheusQuery::ParseByMetricName(const std::string& json) const {
                std::map<std::string, double> out;

                size_t pos = 0;
                while ((pos = json.find("\"__name__\":\"", pos)) != std::string::npos) {
                    pos += 12;
                    auto end = json.find('"', pos);
                    if (end == std::string::npos) break;

                    const std::string name = json.substr(pos, end - pos);

                    auto valPos = json.find("\"value\":[", end);
                    if (valPos == std::string::npos) break;

                    valPos = json.find(',', valPos);
                    if (valPos == std::string::npos) break;
                    valPos++;

                    while (valPos < json.size() && (json[valPos] == ' ' || json[valPos] == '"')) valPos++;
                    auto valEnd = json.find('"', valPos);
                    if (valEnd == std::string::npos) break;

                    try { out[name] += std::stod(json.substr(valPos, valEnd - valPos)); }
                    catch (...) {}

                    pos = valEnd;
                }

                return out;
            }

            double PrometheusQuery::ParseSingleValue(const std::string& json) const {
                // An empty result set means the exporter does not publish this
                // metric - report that rather than a convincing zero.
                auto pos = json.find("\"value\":[");
                if (pos == std::string::npos) return kNoData;

                pos = json.find(',', pos);
                if (pos == std::string::npos) return kNoData;
                pos++;

                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
                auto end = json.find('"', pos);
                if (end == std::string::npos) return kNoData;

                try { return std::stod(json.substr(pos, end - pos)); }
                catch (...) { return kNoData; }
            }

            double PrometheusQuery::GetCached(Query::DataType data) const {
                if (!healthy_)
                    return kNoData;

                std::lock_guard<std::mutex> lock(cacheMutex_);

                auto it = cache_.find(data);
                if (it == cache_.end()) return kNoData;

                // Uptime returns elapsed seconds computed from boot time
                if (data == Query::DataType::Uptime) {
                    if (it->second <= 0.0) return kNoData;
                    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    return static_cast<double>(now) - it->second;
                }

                return it->second;
            }

            std::wstring PrometheusQuery::GetString(const std::string& device, Query::DataType data, const std::string& customQuery) {
                if (data == Query::DataType::Uptime && healthy_) {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    return stringCache_;
                }
                return L"";
            }

            std::wstring PrometheusQuery::FormatUptime(double seconds) const {
                if (seconds <= 0.0) return L"0s";

                long long total = static_cast<long long>(seconds);
                long long d = total / 86400;
                long long h = (total % 86400) / 3600;
                long long m = (total % 3600) / 60;
                long long s = total % 60;

                std::wstringstream ss;
                if (d > 0) ss << d << L"d ";
                if (h > 0) ss << h << L"h ";
                if (m > 0) ss << m << L"m ";
                ss << s << L"s";

                return ss.str();
            }

            std::string PrometheusQuery::HttpGet(const std::string& path) const {
                std::wstring host = Str::ToWide(endpoint_);
                int port = 443;

                if (!proxy_) {
                    auto colon = endpoint_.rfind(':');
                    if (colon == std::string::npos)
                        throw std::runtime_error("endpoint has no port");
                    host = Str::ToWide(endpoint_.substr(0, colon));
                    port = std::stoi(endpoint_.substr(colon + 1));
                }

                std::wstring wpath = Str::ToWide(path);

                HINTERNET hSession = WinHttpOpen(L"NASCP/1.1",
                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
                if (!hSession) throw std::runtime_error("WinHTTP session failed");

                // Without these the defaults are 60s each, so one unreachable host
                // would stall its poller for a minute at a time.
                WinHttpSetTimeouts(hSession, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs);

                HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), static_cast<INTERNET_PORT>(port), 0);
                if (!hConnect) {
                    WinHttpCloseHandle(hSession);
                    throw std::runtime_error("WinHTTP connect failed");
                }

                DWORD flags = proxy_ ? WINHTTP_FLAG_SECURE : 0;

                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
                    nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                if (!hRequest) {
                    WinHttpCloseHandle(hConnect);
                    WinHttpCloseHandle(hSession);
                    throw std::runtime_error("WinHTTP request failed");
                }

                std::string response;
                std::string error;

                if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    error = "send failed (" + std::to_string(GetLastError()) + ")";
                }
                else if (!WinHttpReceiveResponse(hRequest, nullptr)) {
                    error = "no response (" + std::to_string(GetLastError()) + ")";
                }
                else {
                    DWORD status = 0, len = sizeof(status);
                    if (WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)
                        && status != 200) {
                        // A 401 or 500 body still parses as JSON-ish text, so an
                        // unchecked status reads back as plausible numbers.
                        error = "HTTP " + std::to_string(status);
                    }
                    else {
                        DWORD size = 0;
                        do {
                            if (!WinHttpQueryDataAvailable(hRequest, &size)) break;
                            if (size == 0) break;
                            std::string buffer(size, '\0');
                            DWORD read = 0;
                            if (!WinHttpReadData(hRequest, &buffer[0], size, &read)) break;
                            response.append(buffer, 0, read);
                        } while (size > 0);
                    }
                }

                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);

                if (!error.empty())
                    throw std::runtime_error(error);

                return response;
            }

            std::string PrometheusQuery::UrlEncode(const std::string& value) const {
                std::ostringstream encoded;
                for (unsigned char c : value) {
                    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                        encoded << c;
                    else
                        encoded << '%' << std::uppercase << std::hex
                        << std::setw(2) << std::setfill('0') << (int)c;
                }
                return encoded.str();
            }

        } // namespace Prometheus
    } // namespace Providers
} // namespace NASCP
