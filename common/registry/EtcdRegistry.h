#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace im::registry {

// Base64 is used by the etcd v3 JSON gateway for every binary field.
std::string Base64Encode(const std::string& input);
std::string Base64Decode(const std::string& input);

// Registry key layout: im/services/<service>/<instance-id>.
std::string ServiceKey(const std::string& service, const std::string& instance_id);

struct ServiceInstance {
    std::string id;
    std::string host;
    int port = 0;

    std::string ToJson() const;
    static bool FromJson(const std::string& text, ServiceInstance& out);
};

// Minimal etcd v3 client over the HTTP JSON gateway. It only implements the
// calls needed for service registration and discovery, which keeps the test
// stack free of a gRPC etcd dependency.
class EtcdClient {
public:
    explicit EtcdClient(std::string endpoint);

    bool LeaseGrant(int ttl_seconds, std::int64_t& lease_id, std::string& error);
    bool KeepAlive(std::int64_t lease_id, std::string& error);
    bool Revoke(std::int64_t lease_id, std::string& error);
    bool Put(const std::string& key, const std::string& value,
             std::int64_t lease_id, std::string& error);
    bool GetPrefix(const std::string& prefix,
                   std::vector<std::pair<std::string, std::string>>& out,
                   std::string& error);
    bool DeletePrefix(const std::string& prefix, std::string& error);

private:
    bool Post(const std::string& path, const std::string& body,
              std::string& response, std::string& error) const;

    std::string _endpoint;
};

// Registers one instance and keeps its lease alive in a background thread.
class ServiceRegistry {
public:
    ServiceRegistry(std::string endpoint, std::string service,
                    std::string instance_id, std::string host, int port,
                    int ttl_seconds = 10);
    ~ServiceRegistry();

    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    bool Start(std::string& error);
    void Stop();

private:
    void KeepAliveLoop();

    EtcdClient _client;
    std::string _service;
    std::string _instance_id;
    std::string _host;
    int _port;
    int _ttl;
    std::int64_t _lease = 0;
    std::atomic<bool> _running{false};
    std::thread _thread;
    std::mutex _mutex;
    std::condition_variable _cv;
};

// Returns every live instance under im/services/<service>/.
bool Discover(const std::string& endpoint, const std::string& service,
              std::vector<ServiceInstance>& out, std::string& error);

}  // namespace im::registry
