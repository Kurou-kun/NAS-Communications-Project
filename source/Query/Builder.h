#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include "IQuery.h"

namespace NASCP {
    namespace Query {

        class Builder {
        public:
            static IQuery* GetOrCreate(const std::string& type, const std::string& normalizedEndpoint, bool proxy = false);
            static std::string NormalizeEndpoint(const std::string& device, const std::string& type, bool proxy = false);
            static void Shutdown();

        private:
            static std::unordered_map<std::string, std::unique_ptr<IQuery>> clients_;

            static std::string ResolveHost(const std::string& host);
            static std::string DefaultPort(const std::string& type);
        };

    } // namespace Query
} // namespace NASCP