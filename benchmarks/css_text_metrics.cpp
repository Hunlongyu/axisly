#include "css_text_metrics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace axisly::bench {
namespace {
constexpr std::array<std::wstring_view, 5> texts{
    L"Status ready",
    L"Temperature setpoint 24.5 C",
    L"Channel 07 acquisition in progress 128 samples collected",
    L"Changing this label invalidates its ancestors while adjacent panels retain their measured content",
    L"Long diagnostic text wraps when the available inline size becomes smaller during a window resize",
};

css::Measurement synthetic(std::wstring_view value,
                           const css::MeasureRequest& request) {
    std::size_t longest = 0, current = 0;
    for (const wchar_t letter : value) {
        if (letter == L' ') {
            longest = std::max(longest, current);
            current = 0;
        } else ++current;
    }
    const Unit preferred = static_cast<Unit>(value.size() * 7);
    const Unit minimum = static_cast<Unit>(std::max(longest, current) * 7);
    const auto available = request.available_width.definite_value();
    const Unit width = request.known_content_width.value_or(
        request.available_width.kind == css::AvailableKind::MinContent ? minimum
            : available ? std::min(preferred, *available) : preferred);
    const Unit wrap_width = std::max(Unit{1}, request.known_content_width.value_or(
        available.value_or(width)));
    const Unit lines = std::max(Unit{1}, std::ceil(preferred / wrap_width));
    const Unit height = request.known_content_height.value_or(lines * Unit{16});
    return {{width, height}, Unit{12}, height - Unit{4}};
}

#ifdef _WIN32
void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("CSS DirectWrite benchmark measurement failed");
}

struct FontLine {
    Unit width{}, height{}, first_baseline{}, last_baseline{};
};
#endif
} // namespace

struct CssTextMetrics::Impl {
    bool system_font{};
#ifdef _WIN32
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    std::array<FontLine, texts.size()> minimum, maximum;

    [[nodiscard]] FontLine layout(std::wstring_view text, Unit max_width,
                                  bool wrap) const {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> shaped;
        check(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
            format.Get(), std::max(Unit{0.001}, max_width), Unit{1'000'000},
            shaped.GetAddressOf()));
        check(shaped->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
            : DWRITE_WORD_WRAPPING_NO_WRAP));
        DWRITE_TEXT_METRICS metrics{};
        check(shaped->GetMetrics(&metrics));
        std::vector<DWRITE_LINE_METRICS> lines(metrics.lineCount);
        UINT32 actual{};
        check(shaped->GetLineMetrics(lines.data(),
            static_cast<UINT32>(lines.size()), &actual));
        if (!actual) throw std::runtime_error("CSS DirectWrite layout returned no lines");
        return {metrics.widthIncludingTrailingWhitespace, metrics.height,
            lines.front().baseline,
            metrics.height - lines.back().height + lines.back().baseline};
    }
#endif

    explicit Impl(bool real) : system_font(real) {
#ifdef _WIN32
        if (!real) return;
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
        Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
        check(factory->GetSystemFontCollection(fonts.GetAddressOf()));
        UINT32 index{};
        BOOL exists{};
        check(fonts->FindFamilyName(L"Consolas", &index, &exists));
        if (!exists) throw std::runtime_error("Consolas is required for the CSS text benchmark");
        check(factory->CreateTextFormat(L"Consolas", fonts.Get(),
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, Unit{16}, L"en-us", format.GetAddressOf()));
        for (std::size_t i = 0; i < texts.size(); ++i) {
            maximum[i] = layout(texts[i], Unit{1'000'000}, false);
            std::size_t start = 0;
            while (start < texts[i].size()) {
                const auto end = texts[i].find(L' ', start);
                const auto word = texts[i].substr(start,
                    end == std::wstring_view::npos ? end : end - start);
                const auto width = layout(word, Unit{1'000'000}, false);
                if (width.width > minimum[i].width) minimum[i] = width;
                if (end == std::wstring_view::npos) break;
                start = end + 1;
            }
        }
#else
        if (real) throw std::runtime_error("DirectWrite is available only on Windows");
#endif
    }
};

CssTextMetrics::CssTextMetrics(bool system_font)
    : impl_(std::make_unique<Impl>(system_font)) {}
CssTextMetrics::~CssTextMetrics() = default;

css::Measurement CssTextMetrics::measure(std::size_t index,
    std::uint64_t revision, const css::MeasureRequest& request) const {
    const auto variant = (index + static_cast<std::size_t>(revision)) % texts.size();
    if (!impl_->system_font) return synthetic(texts[variant], request);
#ifdef _WIN32
    FontLine measured;
    if (request.available_width.kind == css::AvailableKind::MinContent
        && !request.known_content_width) measured = impl_->minimum[variant];
    else if (request.available_width.kind == css::AvailableKind::MaxContent
        && !request.known_content_width) measured = impl_->maximum[variant];
    else {
        const Unit max_width = request.known_content_width.value_or(
            request.available_width.definite_value().value_or(Unit{1'000'000}));
        measured = impl_->layout(texts[variant], max_width, true);
    }
    const Unit height = request.known_content_height.value_or(measured.height);
    return {{request.known_content_width.value_or(measured.width), height},
        std::clamp(measured.first_baseline, Unit{0}, height),
        std::clamp(measured.last_baseline, Unit{0}, height),
        Size{measured.width, measured.height}};
#else
    throw std::runtime_error("DirectWrite is available only on Windows");
#endif
}

std::string_view CssTextMetrics::name() const noexcept {
    return impl_->system_font ? "directwrite_consolas_16" : "synthetic_wrapping";
}

void CssTextMetrics::check_wrapping() const {
    if (!impl_->system_font) return;
    const auto wide = measure(4, 0, {{}, {},
        css::AvailableSpace::definite(800), css::AvailableSpace::indefinite()});
    const auto narrow = measure(4, 0, {{}, {},
        css::AvailableSpace::definite(80), css::AvailableSpace::indefinite()});
    if (narrow.content_size.height <= wide.content_size.height
        || wide.content_size.width <= 0)
        throw std::runtime_error("CSS DirectWrite text did not wrap at narrow width");
}

} // namespace axisly::bench
