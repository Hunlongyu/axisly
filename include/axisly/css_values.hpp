#pragma once

#include <axisly/geometry.hpp>

#include <bit>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace axisly::css {

namespace detail {
[[noreturn]] void invalid_numeric_value(const char* message);
[[noreturn]] void numeric_value_overflow(const char* message);
} // namespace detail

// A percentage is a fraction: 0.25 means 25 percent. Layout algorithms pass
// the appropriate containing-block basis for each property and axis.
class Calculation {
public:
    [[nodiscard]] static Calculation length(Unit value);
    [[nodiscard]] static Calculation percent(Unit fraction);
    [[nodiscard]] static Calculation add(Calculation left, Calculation right);
    [[nodiscard]] static Calculation subtract(Calculation left, Calculation right);
    [[nodiscard]] static Calculation scale(Calculation value, Unit factor);
    [[nodiscard]] static Calculation minimum(Calculation left, Calculation right);
    [[nodiscard]] static Calculation maximum(Calculation left, Calculation right);
    [[nodiscard]] static Calculation clamp(Calculation lower, Calculation value, Calculation upper);

    // An absent basis leaves percentages unresolved. A definite zero is a
    // present basis and resolves percentages to zero.
    [[nodiscard]] std::optional<Unit> resolve(std::optional<Unit> basis) const;
    [[nodiscard]] bool operator==(const Calculation& other) const noexcept;

private:
    struct Node;
    std::shared_ptr<const Node> root_;
    explicit Calculation(std::shared_ptr<const Node> root) noexcept;
};

enum class LengthKind : std::uint8_t {
    Length,
    Percent,
    Auto,
    MinContent,
    MaxContent,
    FitContent,
    Stretch,
    Content,
    FitContentLength,
    FitContentPercent,
    Calc,
};

class Length {
public:
    Length(const Length& other) noexcept : representation_(other.representation_) {
        if (kind() == LengthKind::Calc) retain_calculation();
    }
    Length(Length&& other) noexcept
        : representation_(std::exchange(other.representation_, auto_representation)) {}
    Length& operator=(const Length& other) noexcept {
        Length copy(other);
        std::swap(representation_, copy.representation_);
        return *this;
    }
    Length& operator=(Length&& other) noexcept {
        Length moved(std::move(other));
        std::swap(representation_, moved.representation_);
        return *this;
    }
    ~Length() {
        if (kind() == LengthKind::Calc) release_calculation();
    }

    [[nodiscard]] static Length length(Unit value);
    [[nodiscard]] static Length percent(Unit fraction);
    [[nodiscard]] static Length automatic() noexcept;
    [[nodiscard]] static Length min_content() noexcept;
    [[nodiscard]] static Length max_content() noexcept;
    [[nodiscard]] static Length fit_content() noexcept;
    [[nodiscard]] static Length stretch() noexcept;
    [[nodiscard]] static Length content() noexcept;
    [[nodiscard]] static Length fit_content_length(Unit limit);
    [[nodiscard]] static Length fit_content_percent(Unit fraction);
    [[nodiscard]] static Length calc(Calculation expression);

    [[nodiscard]] LengthKind kind() const noexcept {
        return static_cast<LengthKind>(representation_ & kind_mask);
    }
    // Resolves only numeric length-percentage and calc values. Auto and
    // intrinsic keywords require the surrounding layout algorithm.
    [[nodiscard]] std::optional<Unit> resolve_numeric(std::optional<Unit> basis) const {
        if (basis && (!std::isfinite(*basis) || *basis < 0)) {
            detail::invalid_numeric_value("CSS percentage basis must be finite and nonnegative");
        }
        if (kind() == LengthKind::Length) return numeric_value();
        if (kind() == LengthKind::Percent && basis) {
            const double result = static_cast<double>(numeric_value()) * *basis;
            if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<Unit>::max()) {
                detail::numeric_value_overflow("CSS calculation exceeds Unit range");
            }
            return static_cast<Unit>(result);
        }
        // Taffy's calc callback requires a definite containing basis, including
        // expressions whose terms happen to contain only fixed lengths.
        if (kind() == LengthKind::Calc && basis) return resolve_calculation(basis);
        return std::nullopt;
    }
    // The fit-content limit is separate from the min/max-content formula.
    [[nodiscard]] std::optional<Unit> resolve_fit_content_limit(std::optional<Unit> basis) const;
    [[nodiscard]] bool operator==(const Length& other) const noexcept;

private:
    // Numeric values store their float bits in the upper half. Expressions use
    // a 16-byte-aligned shared payload, leaving four low bits for the kind.
    // This keeps the common numeric and keyword representation allocation-free.
    struct CalculationStorage;
    static constexpr std::uint64_t kind_mask = 15;
    static constexpr std::uint64_t auto_representation = static_cast<std::uint64_t>(LengthKind::Auto);
    static_assert(sizeof(Unit) == sizeof(std::uint32_t));
    static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));
    static_assert(static_cast<std::uint64_t>(LengthKind::Calc) <= kind_mask);
    std::uint64_t representation_;

    explicit Length(LengthKind kind, Unit value = 0) noexcept
        : representation_((static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value)) << 32)
            | static_cast<std::uint64_t>(kind)) {}
    [[nodiscard]] Unit numeric_value() const noexcept {
        return std::bit_cast<Unit>(static_cast<std::uint32_t>(representation_ >> 32));
    }
    [[nodiscard]] CalculationStorage* calculation_storage() const noexcept;
    void retain_calculation() const noexcept;
    void release_calculation() const noexcept;
    [[nodiscard]] std::optional<Unit> resolve_calculation(std::optional<Unit> basis) const;
};

enum class AvailableKind : std::uint8_t {
    Definite,
    Indefinite,
    MinContent,
    MaxContent,
};

struct AvailableSpace {
    AvailableKind kind{AvailableKind::Indefinite};
    Unit value{};

    [[nodiscard]] static AvailableSpace definite(Unit value) {
        if (!std::isfinite(value)) detail::invalid_numeric_value("CSS numeric value must be finite");
        if (value < 0) detail::invalid_numeric_value("Definite space must be nonnegative");
        return {AvailableKind::Definite, value};
    }
    [[nodiscard]] static AvailableSpace indefinite() noexcept { return {AvailableKind::Indefinite, 0}; }
    [[nodiscard]] static AvailableSpace min_content() noexcept { return {AvailableKind::MinContent, 0}; }
    [[nodiscard]] static AvailableSpace max_content() noexcept { return {AvailableKind::MaxContent, 0}; }
    [[nodiscard]] std::optional<Unit> definite_value() const noexcept {
        return kind == AvailableKind::Definite ? std::optional<Unit>(value) : std::nullopt;
    }
    bool operator==(const AvailableSpace&) const = default;
};

} // namespace axisly::css
