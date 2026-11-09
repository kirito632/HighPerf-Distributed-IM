#include "EtcdRegistry.h"

#include <curl/curl.h>
#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

namespace im::registry {
namespace {

constexpr const char* kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string TrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

// Exclusive range end for a prefix scan, following etcd's convention of
// incrementing the last non-0xFF byte.
std::string PrefixEnd(std::string prefix) {
    for (std::size_t i = prefix.size(); i > 0; --i) {
        auto& byte = prefix[i - 1];
        if (static_cast<unsigned char>(byte) < 0xFF) {
            byte = static_cast<char>(static_cast<unsigned char>(byte) + 1);
            prefix.resize(i);
            return prefix;
        }
    }
    return prefix + '\0';
}

std::size_t WriteCallback(char* data, std::size_t size, std::size_t nmemb,
                          void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

bool ParseJson(const std::string& text, Json::Value& root, std::string& error) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errors;
    if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors)) {
        error = "invalid etcd JSON: " + errors;
        return false;
    }
    return true;
}

std::int64_t AsInt64(const Json::Value& value) {
    if (value.isString()) {
        try {
            return std::stoll(value.asString());
        } catch (...) {
            return 0;
        }
    }
    return value.asInt64();
}

std::string InstanceKey(const std::string& service,
                        const std::string& instance_id) {
    return "im/services/" + service + "/" + instance_id;
}

}  // namespace

std::string Base64Encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < input.size()) {
        const std::uint32_t n =
            (static_cast<unsigned char>(input[i]) << 16) |
            (static_cast<unsigned char>(input[i + 1]) << 8) |
            static_cast<unsigned char>(input[i + 2]);
        out.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(n >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[n & 0x3F]);
        i += 3;
    }
    const std::size_t remaining = input.size() - i;
    if (remaining == 1) {
        const std::uint32_t n = static_cast<unsigned char>(input[i]) << 16;
        out.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        out.append("==");
    } else if (remaining == 2) {
        const std::uint32_t n =
            (static_cast<unsigned char>(input[i]) << 16) |
            (static_cast<unsigned char>(input[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(n >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(n >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string Base64Decode(const std::string& input) {
    auto decode = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=') break;
        const int value = decode(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string ServiceKey(const std::string& service,
                       const std::string& instance_id) {
    std::string safe_id = instance_id;
    std::replace_if(safe_id.begin(), safe_id.end(),
                    [](char c) { return c == '/' || c == ' '; }, '-');
    return InstanceKey(service, safe_id);
}

std::string ServiceInstance::ToJson() const {
    Json::Value root;
    root["id"] = id;
    root["host"] = host;
    root["port"] = port;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

bool ServiceInstance::FromJson(const std::string& text, ServiceInstance& out) {
    Json::Value root;
    std::string error;
    if (!ParseJson(text, root, error) || !root.isObject()) return false;
    out.id = root.get("id", "").asString();
    out.host = root.get("host", "").asString();
    out.port = root.get("port", 0).asInt();
    return !out.id.empty() && !out.host.empty() && out.port > 0;
}

EtcdClient::EtcdClient(std::string endpoint)
    : _endpoint(TrimTrailingSlash(std::move(endpoint))) {}

bool EtcdClient::Post(const std::string& path, const std::string& body,
                      std::string& response, std::string& error) const {
    static std::once_flag curl_once;
    std::call_once(curl_once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    const std::string url = _endpoint + path;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);

    const CURLcode result = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = std::string("etcd request failed: ") + curl_easy_strerror(result);
        return false;
    }
    if (http_code != 200) {
        error = "etcd http " + std::to_string(http_code) + ": " +
                response.substr(0, 200);
        return false;
    }
    return true;
}

bool EtcdClient::LeaseGrant(int ttl_seconds, std::int64_t& lease_id,
                            std::string& error) {
    Json::Value request;
    request["TTL"] = ttl_seconds;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string response;
    if (!Post("/v3/lease/grant", Json::writeString(builder, request), response,
              error)) {
        return false;
    }

    Json::Value root;
    if (!ParseJson(response, root, error)) return false;
    if (root.isMember("error")) {
        error = "etcd lease grant: " + root["error"].asString();
        return false;
    }
    lease_id = AsInt64(root["ID"]);
    if (lease_id == 0) {
        error = "etcd lease grant returned no ID";
        return false;
    }
    return true;
}

bool EtcdClient::KeepAlive(std::int64_t lease_id, std::string& error) {
    Json::Value request;
    request["ID"] = std::to_string(lease_id);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string response;
    if (!Post("/v3/lease/keepalive", Json::writeString(builder, request),
              response, error)) {
        return false;
    }
    return true;
}

bool EtcdClient::Revoke(std::int64_t lease_id, std::string& error) {
    Json::Value request;
    request["ID"] = std::to_string(lease_id);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string response;
    if (!Post("/v3/lease/revoke", Json::writeString(builder, request), response,
              error)) {
        return false;
    }
    return true;
}

bool EtcdClient::Put(const std::string& key, const std::string& value,
                     std::int64_t lease_id, std::string& error) {
    Json::Value request;
    request["key"] = Base64Encode(key);
    request["value"] = Base64Encode(value);
    if (lease_id > 0) {
        request["lease"] = std::to_string(lease_id);
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string response;
    if (!Post("/v3/kv/put", Json::writeString(builder, request), response,
              error)) {
        return false;
    }
    Json::Value root;
    if (!ParseJson(response, root, error)) return false;
    if (root.isMember("error")) {
        error = "etcd put: " + root["error"].asString();
        return false;
    }
    return true;
}

bool EtcdClient::GetPrefix(
    const std::string& prefix,
    std::vector<std::pair<std::string, std::string>>& out, std::string& error) {
    Json::Value request;
    request["key"] = Base64Encode(prefix);
    request["range_end"] = Base64Encode(PrefixEnd(prefix));
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string response;
    if (!Post("/v3/kv/range", Json::writeString(builder, request), response,
              error)) {
        return false;
    }

    Json::Value root;
    if (!ParseJson(response, root, error)) return false;
    if (root.isMember("error")) {
        error = "etcd range: " + root["error"].asString();
        return false;
    }
    const Json::Value& kvs = root["kvs"];
    for (const auto& kv : kvs) {
        out.emplace_back(Base64Decode(kv["key"].asString()),
                         Base64Decode(kv["value"].asString()));
    }
    return true;
}

bool EtcdClient::DeletePrefix(const std::string& prefix, std::string& error) {
    Json::Value request;
    request["key"] = Base64Encode(prefix);
    request["range_end"] = Base64Encode(PrefixEnd(prefix));
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";

    std::string response;
    if (!Post("/v3/kv/deleterange", Json::writeString(builder, request),
              response, error)) {
        return false;
    }
    Json::Value root;
    if (!ParseJson(response, root, error)) return false;
    if (root.isMember("error")) {
        error = "etcd delete: " + root["error"].asString();
        return false;
    }
    return true;
}

ServiceRegistry::ServiceRegistry(std::string endpoint, std::string service,
                                 std::string instance_id, std::string host,
                                 int port, int ttl_seconds)
    : _client(std::move(endpoint)),
      _service(std::move(service)),
      _instance_id(std::move(instance_id)),
      _host(std::move(host)),
      _port(port),
      _ttl(ttl_seconds > 0 ? ttl_seconds : 10) {}

ServiceRegistry::~ServiceRegistry() { Stop(); }

bool ServiceRegistry::Start(std::string& error) {
    if (_running.load()) return true;

    if (!_client.LeaseGrant(_ttl, _lease, error)) return false;

    ServiceInstance instance;
    instance.id = _instance_id;
    instance.host = _host;
    instance.port = _port;
    const std::string key = ServiceKey(_service, _instance_id);
    if (!_client.Put(key, instance.ToJson(), _lease, error)) {
        std::string ignored;
        _client.Revoke(_lease, ignored);
        _lease = 0;
        return false;
    }

    _running.store(true);
    _thread = std::thread([this] { KeepAliveLoop(); });
    return true;
}

void ServiceRegistry::Stop() {
    if (!_running.exchange(false)) return;
    _cv.notify_all();
    if (_thread.joinable()) _thread.join();
    if (_lease != 0) {
        std::string error;
        _client.Revoke(_lease, error);
        _lease = 0;
    }
}

void ServiceRegistry::KeepAliveLoop() {
    const auto interval =
        std::chrono::seconds(std::max(1, _ttl / 3));
    int failures = 0;
    while (_running.load()) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _cv.wait_for(lock, interval, [this] { return !_running.load(); });
        }
        if (!_running.load()) break;

        std::string error;
        if (_client.KeepAlive(_lease, error)) {
            failures = 0;
            continue;
        }

        ++failures;
        std::cerr << "[Etcd] keepalive failed for " << _service << "/"
                  << _instance_id << ": " << error << std::endl;
        if (failures < 2) continue;

        // The lease may have expired (for example if etcd restarted). Try to
        // re-register with a fresh lease so discovery sees the instance again.
        std::int64_t new_lease = 0;
        if (_client.LeaseGrant(_ttl, new_lease, error)) {
            ServiceInstance instance;
            instance.id = _instance_id;
            instance.host = _host;
            instance.port = _port;
            if (_client.Put(ServiceKey(_service, _instance_id),
                            instance.ToJson(), new_lease, error)) {
                _lease = new_lease;
                failures = 0;
                std::cout << "[Etcd] re-registered " << _service << "/"
                          << _instance_id << std::endl;
            }
        }
    }
}

bool Discover(const std::string& endpoint, const std::string& service,
              std::vector<ServiceInstance>& out, std::string& error) {
    EtcdClient client(endpoint);
    std::vector<std::pair<std::string, std::string>> kvs;
    const std::string prefix = "im/services/" + service + "/";
    if (!client.GetPrefix(prefix, kvs, error)) return false;

    for (const auto& kv : kvs) {
        ServiceInstance instance;
        if (ServiceInstance::FromJson(kv.second, instance)) {
            out.push_back(instance);
        }
    }
    return true;
}

}  // namespace im::registry
