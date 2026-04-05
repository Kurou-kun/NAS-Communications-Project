// Plugin.cpp
#include <winsock2.h>
#include <windows.h>
#include "Plugin.h"
#include "Query/Builder.h"
#include "Providers/Prometheus/PrometheusQuery.h"
#include "Utils/Debug.h"
#include "Utils/StringUtils.h"
#include "RainmeterAPI.h"

namespace NASCP {

    static int measureCount_ = 0;

    static Query::DataType ResolveDataType(const std::string& data) {
        std::string lower = Str::ToLower(Str::Trim(data));

        // General
        if (lower == "status")              return Query::DataType::Status;
        if (lower == "uptime")              return Query::DataType::Uptime;
        if (lower == "load")                return Query::DataType::Load;

        // CPU
        if (lower == "cpu_usage")           return Query::DataType::CPU_Usage;
        if (lower == "cpu_temp")            return Query::DataType::CPU_Temp;
        if (lower == "cpu_clock")           return Query::DataType::CPU_Clock;

        // RAM
        if (lower == "ram_percent")         return Query::DataType::RAM_Percent;
        if (lower == "ram_used")            return Query::DataType::RAM_Used;
        if (lower == "ram_total")           return Query::DataType::RAM_Total;
        if (lower == "ram_free")            return Query::DataType::RAM_Free;

        // Swap
        if (lower == "swap_percent")        return Query::DataType::Swap_Percent;
        if (lower == "swap_used")           return Query::DataType::Swap_Used;
        if (lower == "swap_total")          return Query::DataType::Swap_Total;
        if (lower == "swap_free")           return Query::DataType::Swap_Free;

        // Network
        if (lower == "net_download")        return Query::DataType::Net_Download;
        if (lower == "net_upload")          return Query::DataType::Net_Upload;
        if (lower == "net_speed")           return Query::DataType::Net_Speed;
        if (lower == "net_downloadtotal")   return Query::DataType::Net_DownloadTotal;
        if (lower == "net_uploadtotal")     return Query::DataType::Net_UploadTotal;

        // Drive
        if (lower == "drive_usedpercent")   return Query::DataType::Drive_UsedPercent;
        if (lower == "drive_used")          return Query::DataType::Drive_Used;
        if (lower == "drive_free")          return Query::DataType::Drive_Free;
        if (lower == "drive_total")         return Query::DataType::Drive_Total;
        if (lower == "drive_readspeed")     return Query::DataType::Drive_ReadSpeed;
        if (lower == "drive_writespeed")    return Query::DataType::Drive_WriteSpeed;
        if (lower == "drive_read")          return Query::DataType::Drive_Read;
        if (lower == "drive_write")         return Query::DataType::Drive_Write;

        if (lower == "custom")              return Query::DataType::Custom;

        return Query::DataType::Status;
    }

} // namespace NASCP


void Initialize(void** data, void* rm) {
    NASCP::Measure* measure = new NASCP::Measure();
    measure->rm = rm;
    *data = measure;
    NASCP::measureCount_++;
}

void Reload(void* data, void* rm, double* maxValue) {
    NASCP::Measure* measure = static_cast<NASCP::Measure*>(data);
    measure->rm = rm;

    measure->debugLevel = static_cast<int>(RmReadDouble(rm, L"Debug", 0));
    measure->proxy = static_cast<int>(RmReadDouble(rm, L"Proxy", 0)) != 0;

    DEBUG_INFO(rm, measure->debugLevel,
        (std::wstring(L"Reload — Debug=") +
            std::to_wstring(measure->debugLevel) +
            L" Proxy=" + (measure->proxy ? L"1" : L"0")).c_str());

    std::string rawUrl = Str::ToLower(Str::Trim(Str::ToNarrow(RmReadString(rm, L"URL", L"localhost", TRUE))));
    measure->device = Str::ToLower(Str::Trim(Str::ToNarrow(RmReadString(rm, L"Device", L"", TRUE))));
    measure->type = Str::ToLower(Str::Trim(Str::ToNarrow(RmReadString(rm, L"Type", L"prometheus", TRUE))));
    measure->customQuery = Str::Trim(Str::ToNarrow(RmReadString(rm, L"Query", L"", TRUE)));
    std::string dataStr = Str::ToLower(Str::Trim(Str::ToNarrow(RmReadString(rm, L"Data", L"status", TRUE))));

    DEBUG_INFO(rm, measure->debugLevel,
        (L"URL=" + Str::ToWide(rawUrl) +
            L" Device=" + Str::ToWide(measure->device) +
            L" Type=" + Str::ToWide(measure->type) +
            L" Data=" + Str::ToWide(dataStr) +
            L" Query=" + Str::ToWide(measure->customQuery)).c_str());

    measure->url = NASCP::Query::Builder::NormalizeEndpoint(rawUrl, measure->type, measure->proxy);

    DEBUG_INFO(rm, measure->debugLevel,
        (L"Normalized endpoint: " + Str::ToWide(measure->url)).c_str());

    measure->dataType = NASCP::ResolveDataType(dataStr);

    DEBUG_INFO(rm, measure->debugLevel,
        (L"Resolved DataType index: " + std::to_wstring(static_cast<int>(measure->dataType))).c_str());

    if (!measure->customQuery.empty()) {
        measure->dataType = NASCP::Query::DataType::Custom;
        DEBUG_INFO(rm, measure->debugLevel, L"Custom query override active");
    }

    measure->query = NASCP::Query::Builder::GetOrCreate(measure->type, measure->url, measure->proxy);

    if (measure->query) {
        if (auto* pq = dynamic_cast<NASCP::Providers::Prometheus::PrometheusQuery*>(measure->query))
            pq->SetDebug(rm, measure->debugLevel);

        DEBUG_INFO(rm, measure->debugLevel,
            (L"Client ready for endpoint: " + Str::ToWide(measure->url)).c_str());
    }
    else {
        DEBUG_ERROR(rm, measure->debugLevel,
            (L"Failed to create client for Type=" + Str::ToWide(measure->type) +
                L" Endpoint=" + Str::ToWide(measure->url)).c_str());
    }
}

double Update(void* data) {
    NASCP::Measure* measure = static_cast<NASCP::Measure*>(data);

    if (!measure->query) {
        DEBUG_ERROR(measure->rm, measure->debugLevel, L"Update called but query client is null");
        return 0.0;
    }

    return measure->query->Fetch(measure->device, measure->dataType, measure->customQuery);
}

LPCWSTR GetString(void* data) {
    NASCP::Measure* measure = static_cast<NASCP::Measure*>(data);

    if (!measure->query)
        return nullptr;

    static std::wstring result;
    result = measure->query->GetString(measure->device, measure->dataType, measure->customQuery);

    if (result.empty())
        return nullptr;

    return result.c_str();
}

void Finalize(void* data) {
    NASCP::Measure* measure = static_cast<NASCP::Measure*>(data);

    DEBUG_INFO(measure->rm, measure->debugLevel, L"Finalize called");

    delete measure;

    NASCP::measureCount_--;
    if (NASCP::measureCount_ <= 0) {
        NASCP::Query::Builder::Shutdown();
        NASCP::measureCount_ = 0;
    }
}