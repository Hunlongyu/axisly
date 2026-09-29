#pragma once

#include <axisly/css_measurement.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace axisly::bench {

// Benchmark-only host text measurement. The system-font backend is available
// on Windows; the synthetic backend keeps the fixture portable.
class CssTextMetrics {
public:
    explicit CssTextMetrics(bool system_font);
    ~CssTextMetrics();
    CssTextMetrics(const CssTextMetrics&) = delete;
    CssTextMetrics& operator=(const CssTextMetrics&) = delete;

    [[nodiscard]] css::Measurement measure(std::size_t index,
        std::uint64_t revision, const css::MeasureRequest& request) const;
    [[nodiscard]] std::string_view name() const noexcept;
    void check_wrapping() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace axisly::bench
