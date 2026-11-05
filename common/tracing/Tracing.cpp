#include "Tracing.h"

#include <map>
#include <mutex>
#include <utility>

#ifdef IM_WITH_OPENTELEMETRY
#include <opentelemetry/exporters/otlp/otlp_http_exporter.h>
#include <opentelemetry/sdk/trace/batch_span_processor.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/context/propagation/text_map_propagator.h>
#include <opentelemetry/trace/propagation/http_trace_context.h>
#endif

namespace im::tracing {
namespace {

std::mutex& StateMutex() {
    static std::mutex mutex;
    return mutex;
}

bool& Enabled() {
    static bool enabled = false;
    return enabled;
}

#ifdef IM_WITH_OPENTELEMETRY
std::shared_ptr<opentelemetry::sdk::trace::TracerProvider>& Provider() {
    static std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> provider;
    return provider;
}

#endif

#ifdef IM_WITH_OPENTELEMETRY
class Carrier : public opentelemetry::context::propagation::TextMapCarrier {
public:
    opentelemetry::nostd::string_view Get(opentelemetry::nostd::string_view key) const noexcept override {
        auto it = values.find(std::string(key));
        return it == values.end() ? opentelemetry::nostd::string_view{} : it->second;
    }

    void Set(opentelemetry::nostd::string_view key,
             opentelemetry::nostd::string_view value) noexcept override {
        values[std::string(key)] = std::string(value);
    }

    std::map<std::string, std::string> values;
    std::string GetOwned(opentelemetry::nostd::string_view key) const {
        auto it = values.find(std::string(key));
        return it == values.end() ? std::string{} : it->second;
    }
};

auto Tracer() {
    return opentelemetry::trace::Provider::GetTracerProvider()->GetTracer(
        "im-services", "1.0.0");
}
#endif

}  // namespace

struct Span::Impl {
#ifdef IM_WITH_OPENTELEMETRY
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span> span;
#endif
};

Span::Span(std::unique_ptr<Impl> impl) : _impl(std::move(impl)) {}
Span::~Span() { End(); }

void Span::SetAttribute(const std::string& key, const std::string& value) {
#ifdef IM_WITH_OPENTELEMETRY
    if (_impl && _impl->span) _impl->span->SetAttribute(key, value);
#else
    (void)key;
    (void)value;
#endif
}

void Span::SetAttribute(const std::string& key, std::int64_t value) {
#ifdef IM_WITH_OPENTELEMETRY
    if (_impl && _impl->span) _impl->span->SetAttribute(key, value);
#else
    (void)key;
    (void)value;
#endif
}

std::string Span::Traceparent() const {
#ifdef IM_WITH_OPENTELEMETRY
    if (_impl && _impl->span) {
        Carrier carrier;
        opentelemetry::context::Context context;
        context = opentelemetry::trace::SetSpan(context, _impl->span);
        opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator()
            ->Inject(carrier, context);
        return carrier.GetOwned("traceparent");
    }
#endif
    return {};
}

void Span::End() {
#ifdef IM_WITH_OPENTELEMETRY
    if (_impl && _impl->span) {
        _impl->span->End();
        _impl->span.reset();
    }
#endif
}

bool Init(bool enabled, const std::string& endpoint,
          const std::string& service_name, std::string& error) {
    std::lock_guard<std::mutex> lock(StateMutex());
    Enabled() = false;
    if (!enabled) return true;
#ifndef IM_WITH_OPENTELEMETRY
    (void)endpoint;
    (void)service_name;
    error = "OpenTelemetry support was not built (configure with IM_WITH_OPENTELEMETRY=ON)";
    return false;
#else
    try {
        opentelemetry::exporter::otlp::OtlpHttpExporterOptions exporter_options;
        exporter_options.url = endpoint;
        auto exporter = std::make_unique<opentelemetry::exporter::otlp::OtlpHttpExporter>(
            exporter_options);
        opentelemetry::sdk::trace::BatchSpanProcessorOptions processor_options;
        auto processor = std::make_unique<opentelemetry::sdk::trace::BatchSpanProcessor>(
            std::move(exporter), processor_options);
        auto provider = std::make_shared<opentelemetry::sdk::trace::TracerProvider>(
            std::move(processor),
            opentelemetry::sdk::resource::Resource::Create({{"service.name", service_name}}));
        Provider() = provider;
        opentelemetry::trace::Provider::SetTracerProvider(provider);
        opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
            opentelemetry::nostd::shared_ptr<opentelemetry::context::propagation::TextMapPropagator>(
                new opentelemetry::trace::propagation::HttpTraceContext()));
        Enabled() = true;
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
#endif
}

std::unique_ptr<Span> StartSpan(const std::string& name,
                                const std::string& parent_traceparent) {
    std::lock_guard<std::mutex> lock(StateMutex());
    if (!Enabled()) return nullptr;
    auto impl = std::make_unique<Span::Impl>();
#ifdef IM_WITH_OPENTELEMETRY
    opentelemetry::trace::StartSpanOptions options;
    std::unique_ptr<opentelemetry::context::Context> parent_context;
    if (!parent_traceparent.empty()) {
        Carrier carrier;
        carrier.Set("traceparent", parent_traceparent);
        parent_context = std::make_unique<opentelemetry::context::Context>();
        *parent_context = opentelemetry::trace::propagation::HttpTraceContext().Extract(
            carrier, *parent_context);
        auto parent_span = opentelemetry::trace::GetSpan(*parent_context);
        if (parent_span && parent_span->GetContext().IsValid()) {
            options.parent = opentelemetry::trace::SetSpan(*parent_context, parent_span);
        }
    }
    impl->span = Tracer()->StartSpan(name, options);
#endif
    return std::unique_ptr<Span>(new Span(std::move(impl)));
}

void Shutdown() {
#ifdef IM_WITH_OPENTELEMETRY
    std::lock_guard<std::mutex> lock(StateMutex());
    if (Enabled()) {
        if (Provider()) Provider()->Shutdown();
        Provider().reset();
        Enabled() = false;
    }
#endif
}

}  // namespace im::tracing
