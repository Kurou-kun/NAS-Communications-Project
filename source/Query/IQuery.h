#pragma once
#include <string>

namespace NASCP {
    namespace Query {

        enum class DataType {
            // General
            Status,
            Uptime,
            Load,

            // CPU
            CPU_Usage,
            CPU_Temp,
            CPU_Clock,

            // RAM
            RAM_Percent,
            RAM_Used,
            RAM_Total,
            RAM_Free,

            // Swap
            Swap_Percent,
            Swap_Used,
            Swap_Total,
            Swap_Free,

            // Network
            Net_Download,
            Net_Upload,
            Net_Speed,
            Net_DownloadTotal,
            Net_UploadTotal,

            // Drive (/ only)
            Drive_UsedPercent,
            Drive_Used,
            Drive_Free,
            Drive_Total,
            Drive_ReadSpeed,
            Drive_WriteSpeed,
            Drive_Read,
            Drive_Write,

            Custom
        };

        class IQuery {
        public:
            virtual ~IQuery() = default;

            virtual double Fetch(const std::string& device, DataType data, const std::string& customQuery = "") = 0;
            virtual std::wstring GetString(const std::string& device, DataType data, const std::string& customQuery = "") = 0;
            virtual bool IsAvailable() = 0;
            virtual void SetEndpoint(const std::string& endpoint, bool proxy = false) = 0;
        };

    } // namespace Query
} // namespace NASCP