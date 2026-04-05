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

            PrometheusQuery::~PrometheusQuery() {
                shutdown_ = true;
                if (workerThread_.joinable())
                    workerThread_.join();
            }

            void PrometheusQuery::SetEndpoint(const std::string& endpoint, bool proxy) {
                endpoint_ = endpoint;
                proxy_ = proxy;
                lastFetch_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(200);
            }

            void PrometheusQuery::SetDebug(void* rm, int debugLevel) {
                rm_ = rm;
                debugLevel_ = debugLevel;
            }

            bool PrometheusQuery::IsAvailable() {
                try {
                    HttpGet("/api/v1/query?query=up");
                    return true;
                }
                catch (...) {
                    return false;
                }
            }

            double PrometheusQuery::Fetch(const std::string& device, Query::DataType data, const std::string& customQuery) {
                auto now = std::chrono::steady_clock::now();

                {
                    std::lock_guard<std::mutex> lock(fetchMutex_);
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFetch_).count();

                    if (!requestPending_ && elapsed >= 100) {
                        requestPending_ = true;
                        lastFetch_ = now;

                        if (workerThread_.joinable())
                            workerThread_.join();

                        if (data == Query::DataType::Custom && !customQuery.empty()) {
                            workerThread_ = std::thread([this, customQuery]() {
                                if (shutdown_) { requestPending_ = false; return; }
                                try {
                                    std::string response = HttpGet("/api/v1/query?query=" + UrlEncode(customQuery));
                                    double result = ParseSingleValue(response);
                                    std::lock_guard<std::mutex> lock(cacheMutex_);
                                    cache_[Query::DataType::Custom] = result;
                                }
                                catch (const std::exception& e) {
                                    DEBUG_ERROR(rm_, debugLevel_, (L"Custom query error: " + Str::ToWide(e.what())).c_str());
                                }
                                catch (...) {
                                    DEBUG_ERROR(rm_, debugLevel_, L"Custom query unknown error");
                                }
                                requestPending_ = false;
                                });
                        }
                        else {
                            workerThread_ = std::thread(&PrometheusQuery::AsyncFetch, this, device);
                        }
                    }
                }

                return GetCached(data);
            }

            void PrometheusQuery::AsyncFetch(const std::string& device) {
                if (shutdown_) { requestPending_ = false; return; }

                try {
                    // ── Raw metrics ────────────────────────────────────────────────────
                    std::string rawResponse = HttpGet("/api/v1/query?query=" + UrlEncode(BuildRawQuery(device)));
                    ParseRawResponse(rawResponse, device);

                    // ── Update uptime string cache ─────────────────────────────────────
                    {
                        std::lock_guard<std::mutex> lock(cacheMutex_);
                        if (cache_.count(Query::DataType::Uptime)) {
                            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            double uptime = static_cast<double>(now) - cache_[Query::DataType::Uptime];
                            stringCache_ = FormatUptime(uptime);
                        }
                    }

                    // ── CPU Usage ──────────────────────────────────────────────────────
                    std::string cpuUsageResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "100 - avg by (job) (rate(node_cpu_seconds_total{job=\"" + device + "\",mode=\"idle\"}[1m])) * 100"));

                    // ── CPU Clock ──────────────────────────────────────────────────────
                    std::string cpuClockResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "avg by (job) (node_cpu_scaling_frequency_hertz{job=\"" + device + "\"})"));

                    // ── CPU Temp (AMD via PCI chip) ────────────────────────────────────
                    std::string cpuTempResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "avg(node_hwmon_temp_celsius{job=\"" + device + "\",chip=~\"coretemp.*|pci.*\",sensor=\"temp1\"})"));

                    // ── Network ────────────────────────────────────────────────────────
                    std::string netDownloadResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (rate(node_network_receive_bytes_total{job=\"" + device + "\",device!~\"lo|docker.*|veth.*|br.*|virbr.*\"}[1m]))"));

                    std::string netUploadResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (rate(node_network_transmit_bytes_total{job=\"" + device + "\",device!~\"lo|docker.*|veth.*|br.*|virbr.*\"}[1m]))"));

                    std::string netSpeedResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "max by (job) (node_network_speed_bytes{job=\"" + device + "\",device!~\"lo|docker.*|veth.*|br.*|virbr.*\"})"));

                    std::string netDownTotalResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (node_network_receive_bytes_total{job=\"" + device + "\",device!~\"lo|docker.*|veth.*|br.*|virbr.*\"})"));

                    std::string netUpTotalResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (node_network_transmit_bytes_total{job=\"" + device + "\",device!~\"lo|docker.*|veth.*|br.*|virbr.*\"})"));

                    // ── Drive (/ only) ─────────────────────────────────────────────────
                    std::string driveUsedPercentResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "(1 - node_filesystem_avail_bytes{job=\"" + device + "\",mountpoint=\"/\",fstype!~\"tmpfs|overlay\"} / node_filesystem_size_bytes{job=\"" + device + "\",mountpoint=\"/\",fstype!~\"tmpfs|overlay\"}) * 100"));

                    std::string driveUsedResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "node_filesystem_size_bytes{job=\"" + device + "\",mountpoint=\"/\",fstype!~\"tmpfs|overlay\"} - node_filesystem_avail_bytes{job=\"" + device + "\",mountpoint=\"/\",fstype!~\"tmpfs|overlay\"}"));

                    std::string driveTotalResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "node_filesystem_size_bytes{job=\"" + device + "\",mountpoint=\"/\",fstype!~\"tmpfs|overlay\"}"));

                    std::string driveFreeResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "node_filesystem_avail_bytes{job=\"" + device + "\",mountpoint=\"/\",fstype!~\"tmpfs|overlay\"}"));

                    std::string driveReadSpeedResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (rate(node_disk_read_bytes_total{job=\"" + device + "\"}[1m]))"));

                    std::string driveWriteSpeedResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (rate(node_disk_written_bytes_total{job=\"" + device + "\"}[1m]))"));

                    std::string driveReadResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (node_disk_read_bytes_total{job=\"" + device + "\"})"));

                    std::string driveWriteResponse = HttpGet("/api/v1/query?query=" + UrlEncode(
                        "sum by (job) (node_disk_written_bytes_total{job=\"" + device + "\"})"));

                    // ── Store all results ──────────────────────────────────────────────
                    {
                        std::lock_guard<std::mutex> lock(cacheMutex_);

                        cache_[Query::DataType::CPU_Usage] = ParseSingleValue(cpuUsageResponse);
                        cache_[Query::DataType::CPU_Clock] = ParseSingleValue(cpuClockResponse);
                        cache_[Query::DataType::CPU_Temp] = ParseSingleValue(cpuTempResponse);

                        cache_[Query::DataType::Net_Download] = ParseSingleValue(netDownloadResponse);
                        cache_[Query::DataType::Net_Upload] = ParseSingleValue(netUploadResponse);
                        cache_[Query::DataType::Net_Speed] = ParseSingleValue(netSpeedResponse);
                        cache_[Query::DataType::Net_DownloadTotal] = ParseSingleValue(netDownTotalResponse);
                        cache_[Query::DataType::Net_UploadTotal] = ParseSingleValue(netUpTotalResponse);

                        cache_[Query::DataType::Drive_UsedPercent] = ParseSingleValue(driveUsedPercentResponse);
                        cache_[Query::DataType::Drive_Used] = ParseSingleValue(driveUsedResponse);
                        cache_[Query::DataType::Drive_Total] = ParseSingleValue(driveTotalResponse);
                        cache_[Query::DataType::Drive_Free] = ParseSingleValue(driveFreeResponse);
                        cache_[Query::DataType::Drive_ReadSpeed] = ParseSingleValue(driveReadSpeedResponse);
                        cache_[Query::DataType::Drive_WriteSpeed] = ParseSingleValue(driveWriteSpeedResponse);
                        cache_[Query::DataType::Drive_Read] = ParseSingleValue(driveReadResponse);
                        cache_[Query::DataType::Drive_Write] = ParseSingleValue(driveWriteResponse);
                    }

                }
                catch (const std::exception& e) {
                    DEBUG_ERROR(rm_, debugLevel_, (L"AsyncFetch error: " + Str::ToWide(e.what())).c_str());
                }
                catch (...) {
                    DEBUG_ERROR(rm_, debugLevel_, L"AsyncFetch unknown error");
                }

                requestPending_ = false;
            }

            std::string PrometheusQuery::BuildRawQuery(const std::string& device) const {
                return "{job=\"" + device + "\",__name__=~\"up|node_memory_MemAvailable_bytes|node_memory_MemTotal_bytes|node_memory_SwapTotal_bytes|node_memory_SwapFree_bytes|node_boot_time_seconds|node_load1\"}";
            }

            void PrometheusQuery::ParseRawResponse(const std::string& json, const std::string& device) {
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
                    else if (name == "node_memory_MemAvailable_bytes")
                        cache_[Query::DataType::RAM_Free] = value;
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

            double PrometheusQuery::ParseSingleValue(const std::string& json) const {
                auto pos = json.find("\"value\":[");
                if (pos == std::string::npos) return 0.0;

                pos = json.find(',', pos);
                if (pos == std::string::npos) return 0.0;
                pos++;

                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '"')) pos++;
                auto end = json.find('"', pos);
                if (end == std::string::npos) return 0.0;

                try { return std::stod(json.substr(pos, end - pos)); }
                catch (...) { return 0.0; }
            }

            double PrometheusQuery::GetCached(Query::DataType data) const {
                std::lock_guard<std::mutex> lock(cacheMutex_);

                auto it = cache_.find(data);
                if (it == cache_.end()) return 0.0;

                // Uptime returns elapsed seconds computed from boot time
                if (data == Query::DataType::Uptime) {
                    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    return static_cast<double>(now) - it->second;
                }

                return it->second;
            }

            std::wstring PrometheusQuery::GetString(const std::string& device, Query::DataType data, const std::string& customQuery) {
                if (data == Query::DataType::Uptime) {
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

                std::wostringstream ss;
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
                    host = Str::ToWide(endpoint_.substr(0, colon));
                    port = std::stoi(endpoint_.substr(colon + 1));
                }

                std::wstring wpath = Str::ToWide(path);

                HINTERNET hSession = WinHttpOpen(L"NASCP/1.0",
                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
                if (!hSession) throw std::runtime_error("WinHTTP session failed");

                HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
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

                WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                WinHttpReceiveResponse(hRequest, nullptr);

                std::string response;
                DWORD size = 0;
                do {
                    WinHttpQueryDataAvailable(hRequest, &size);
                    if (size == 0) break;
                    std::string buffer(size, '\0');
                    DWORD read = 0;
                    WinHttpReadData(hRequest, &buffer[0], size, &read);
                    response += buffer.substr(0, read);
                } while (size > 0);

                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);

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