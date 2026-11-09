// Small etcd registry helper used by integration tests and demos.
//
//   registry_probe discover --service chatserver
//   registry_probe register --service chatserver --id probe --host 127.0.0.1 \
//       --port 19999 --ttl 30 --hold 2
//
// The register mode keeps the lease alive for --hold seconds, then revokes it
// so the key disappears immediately instead of waiting for the TTL.

#include "EtcdRegistry.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string EnvOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

void Usage() {
    std::cerr << "usage:\n"
              << "  registry_probe discover --service <name> [--endpoint <url>]\n"
              << "  registry_probe register --service <name> --id <id> --host <host>"
                 " --port <port> [--ttl 30] [--hold 2] [--endpoint <url>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        Usage();
        return 2;
    }

    const std::string mode = argv[1];
    std::map<std::string, std::string> args;
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string key = argv[i];
        if (key.rfind("--", 0) == 0) key = key.substr(2);
        args[key] = argv[i + 1];
    }

    const std::string service = args.count("service") ? args["service"] : "";
    const std::string endpoint = args.count("endpoint")
                                     ? args["endpoint"]
                                     : EnvOr("IM_ETCD_ENDPOINT",
                                             "http://127.0.0.1:12379");
    if (service.empty()) {
        Usage();
        return 2;
    }

    if (mode == "discover") {
        std::vector<im::registry::ServiceInstance> instances;
        std::string error;
        if (!im::registry::Discover(endpoint, service, instances, error)) {
            std::cerr << "discover failed: " << error << std::endl;
            return 1;
        }
        for (const auto& instance : instances) {
            std::cout << instance.id << " " << instance.host << " "
                      << instance.port << std::endl;
        }
        std::cout << "count=" << instances.size() << std::endl;
        return 0;
    }

    if (mode == "register") {
        const std::string id = args.count("id") ? args["id"] : "";
        const std::string host = args.count("host") ? args["host"] : "127.0.0.1";
        const int port = args.count("port") ? std::stoi(args["port"]) : 0;
        const int ttl = args.count("ttl") ? std::stoi(args["ttl"]) : 30;
        const int hold = args.count("hold") ? std::stoi(args["hold"]) : 2;
        if (id.empty() || port <= 0) {
            Usage();
            return 2;
        }

        im::registry::ServiceRegistry registry(endpoint, service, id, host, port,
                                               ttl);
        std::string error;
        if (!registry.Start(error)) {
            std::cerr << "register failed: " << error << std::endl;
            return 1;
        }
        std::cout << "registered " << im::registry::ServiceKey(service, id)
                  << " (" << host << ":" << port << ")" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(std::max(0, hold)));
        registry.Stop();
        std::cout << "revoked" << std::endl;
        return 0;
    }

    Usage();
    return 2;
}
