#include <windows.h>
#include "Builder.h"
#include "Providers/Prometheus/PrometheusQuery.h"
#include <stdexcept>

namespace NASCP {
    namespace Query {

        std::unordered_map<std::string, std::unique_ptr<IQuery>>& Builder::Clients() {
            // Deliberately leaked. As a plain static this map would be destroyed
            // at process detach - inside DllMain, under the loader lock - and each
            // client's destructor joins its poller thread, which is exactly the
            // deadlock shape that froze Rainmeter in the sibling plugin. Clients
            // are released explicitly in Shutdown(), called from Finalize.
            static auto* clients = new std::unordered_map<std::string, std::unique_ptr<IQuery>>();
            return *clients;
        }

        IQuery* Builder::GetOrCreate(const std::string& type, const std::string& normalizedEndpoint,
                                     const std::string& device, const std::string& mount, bool proxy) {
            // The key carries the device and the mount as well as the endpoint: a
            // client owns a value cache keyed by metric only, so sharing one
            // between devices would make them overwrite each other's readings, and
            // measures that leave Mount= at its default would reset the setting of
            // one that did not.
            const std::string key = normalizedEndpoint + "|" + device + "|" + mount;

            auto& clients = Clients();

            auto it = clients.find(key);
            if (it != clients.end())
                return it->second.get();

            std::unique_ptr<IQuery> client;

            if (type == "prometheus")
                client = std::make_unique<NASCP::Providers::Prometheus::PrometheusQuery>();
            else
                return nullptr;

            client->SetEndpoint(normalizedEndpoint, proxy);
            clients[key] = std::move(client);
            return clients[key].get();
        }

        std::string Builder::NormalizeEndpoint(const std::string& device, const std::string& type, bool proxy) {
            if (proxy)
                return device;

            // The hostname is handed to WinHTTP as-is. Resolving it here bought
            // nothing, cost a WSAStartup/WSACleanup pair per call, and was
            // IPv4-only.
            auto colon = device.rfind(':');
            if (colon != std::string::npos)
                return device;

            return device + ":" + DefaultPort(type);
        }

        std::string Builder::DefaultPort(const std::string& type) {
            if (type == "prometheus") return "9090";
            return "80";
        }

        void Builder::Shutdown() {
            auto& clients = Clients();

            // Signal every poller first so they wind down in parallel, then
            // release them - otherwise each destructor would wait out its own
            // in-flight request in turn, on Rainmeter's thread.
            for (auto& entry : clients)
                if (entry.second) entry.second->Stop();

            clients.clear();
        }

    } // namespace Query
} // namespace NASCP
