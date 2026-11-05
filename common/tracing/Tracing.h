#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace im::tracing {

class Span {
public:
    ~Span();
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;

    void SetAttribute(const std::string& key, const std::string& value);
    void SetAttribute(const std::string& key, std::int64_t value);
    std::string Traceparent() const;
    void End();

private:
    friend std::unique_ptr<Span> StartSpan(const std::string&, const std::string&);
    struct Impl;
    explicit Span(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> _impl;
};

bool Init(bool enabled, const std::string& endpoint,
          const std::string& service_name, std::string& error);
std::unique_ptr<Span> StartSpan(const std::string& name,
                                const std::string& parent_traceparent = {});
void Shutdown();

}  // namespace im::tracing
