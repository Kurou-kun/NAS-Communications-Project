// Plugin.h
#pragma once
#include <string>
#include "Query/IQuery.h"
#include "Query/Builder.h"
#include "RainmeterAPI.h"

namespace NASCP {

    struct Measure {
        Query::IQuery* query = nullptr;
        std::string url;
        std::string device;
        std::string type;
        Query::DataType dataType;
        std::string customQuery;
        int debugLevel = 0;
        bool proxy = false;
        void* rm = nullptr;
    };

} // namespace NASCP

PLUGIN_EXPORT void Initialize(void** data, void* rm);
PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue);
PLUGIN_EXPORT double Update(void* data);
PLUGIN_EXPORT LPCWSTR GetString(void* data);
PLUGIN_EXPORT void Finalize(void* data);