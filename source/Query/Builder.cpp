#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "Builder.h"
#include "Providers/Prometheus/PrometheusQuery.h"
#include <stdexcept>

#pragma comment(lib, "ws2_32.lib")

namespace NASCP {
    namespace Query {

        std::unordered_map<std::string, std::unique_ptr<IQuery>> Builder::clients_;

        IQuery* Builder::GetOrCreate(const std::string& type, const std::string& normalizedEndpoint, const std::string& device, bool proxy) {
            // clients_ is static, so the key has to carry the device too -
            // otherwise two skins reading different devices from the same
            // Prometheus share one cache and overwrite each other.
            const std::string key = normalizedEndpoint + "|" + device;

            auto it = clients_.find(key);
            if (it != clients_.end())
                return it->second.get();

            std::unique_ptr<IQuery> client;

            if (type == "prometheus")
                client = std::make_unique<NASCP::Providers::Prometheus::PrometheusQuery>();
            else
                return nullptr;

            client->SetEndpoint(normalizedEndpoint, proxy);
            clients_[key] = std::move(client);
            return clients_[key].get();
        }

        std::string Builder::NormalizeEndpoint(const std::string& device, const std::string& type, bool proxy) {
            if (proxy)
                return device;

            std::string host;
            std::string port;

            auto colon = device.rfind(':');
            if (colon != std::string::npos) {
                host = device.substr(0, colon);
                port = device.substr(colon + 1);
            }
            else {
                host = device;
                port = DefaultPort(type);
            }

            std::string resolved = ResolveHost(host);
            return resolved + ":" + port;
        }

        std::string Builder::ResolveHost(const std::string& host) {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);

            addrinfo hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            addrinfo* result = nullptr;
            if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) {
                WSACleanup();
                return host;
            }

            char ip[INET_ADDRSTRLEN];
            sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));

            freeaddrinfo(result);
            WSACleanup();

            return std::string(ip);
        }

        std::string Builder::DefaultPort(const std::string& type) {
            if (type == "prometheus") return "9090";
            return "80";
        }

        void Builder::Shutdown() {
            clients_.clear();
        }

    } // namespace Query
} // namespace NASCP