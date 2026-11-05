#include "Metrics.h"

#include <map>
#include <mutex>
#include <sstream>

namespace im::metrics {
namespace {

struct Metric {
    std::string help;
    std::string type;  // counter | gauge
    std::int64_t value = 0;
};

std::mutex& Mutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, Metric>& Registry() {
    static std::map<std::string, Metric> registry;
    return registry;
}

}  // namespace

void IncCounter(const std::string& name, std::uint64_t delta,
                const std::string& help) {
    std::lock_guard<std::mutex> lock(Mutex());
    Metric& metric = Registry()[name];
    if (metric.type.empty()) {
        metric.type = "counter";
        metric.help = help;
    }
    metric.value += static_cast<std::int64_t>(delta);
}

void SetGauge(const std::string& name, std::int64_t value,
              const std::string& help) {
    std::lock_guard<std::mutex> lock(Mutex());
    Metric& metric = Registry()[name];
    metric.type = "gauge";
    metric.help = help;
    metric.value = value;
}

std::string RenderPrometheus() {
    std::lock_guard<std::mutex> lock(Mutex());
    std::ostringstream out;
    for (const auto& entry : Registry()) {
        const std::string& name = entry.first;
        const Metric& metric = entry.second;
        if (!metric.help.empty()) {
            out << "# HELP " << name << " " << metric.help << "\n";
        }
        out << "# TYPE " << name << " " << metric.type << "\n";
        out << name << " " << metric.value << "\n";
    }
    return out.str();
}

}  // namespace im::metrics
