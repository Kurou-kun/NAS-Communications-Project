#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include "IQuery.h"

namespace NASCP {
    namespace Query {

        class Builder {
        public:
            // One client per endpoint + device + mount. The client owns a value
            // cache keyed only by metric, so sharing it between devices would make
            // them overwrite each other's readings; and since every measure pushes
            // its own Mount= on reload, two measures of the same host with
            // different mounts would otherwise fight over one client's setting.
            static IQuery* GetOrCreate(const std::string& type, const std::string& normalizedEndpoint,
                                       const std::string& device, const std::string& mount, bool proxy = false);
            static std::string NormalizeEndpoint(const std::string& device, const std::string& type, bool proxy = false);
            static void Shutdown();

        private:
            // Accessor rather than a static member: the map must never be
            // destroyed at process detach. See Builder.cpp.
            static std::unordered_map<std::string, std::unique_ptr<IQuery>>& Clients();

            static std::string DefaultPort(const std::string& type);
        };

    } // namespace Query
} // namespace NASCP