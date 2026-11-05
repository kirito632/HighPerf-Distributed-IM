#pragma once

#include <cstdint>
#include <string>

namespace im::metrics {

void IncCounter(const std::string& name, std::uint64_t delta,
                const std::string& help);
void SetGauge(const std::string& name, std::int64_t value,
              const std::string& help);

// Renders Prometheus text exposition format (no labels).
std::string RenderPrometheus();

}  // namespace im::metrics
