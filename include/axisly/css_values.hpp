#pragma once

#include <axisly/geometry.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace axisly::css {

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

    [[nodiscard]] LengthKind kind() const noexcept { return kind_; }
    // Resolves only numeric length-percentage and calc values. Auto and
    // intrinsic keywords require the surrounding layout algorithm.
    [[nodiscard]] std::optional<Unit> resolve_numeric(std::optional<Unit> basis) const;
    // The fit-content limit is separate from the min/max-content formula.
    [[nodiscard]] std::optional<Unit> resolve_fit_content_limit(std::optional<Unit> basis) const;
    [[nodiscard]] bool operator==(const Length& other) const noexcept;

private:
    LengthKind kind_{LengthKind::Auto};
    Unit value_{};
    std::optional<Calculation> calculation_;
    explicit Length(LengthKind kind, Unit value = 0) noexcept;
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

    [[nodiscard]] static AvailableSpace definite(Unit value);
    [[nodiscard]] static AvailableSpace indefinite() noexcept;
    [[nodiscard]] static AvailableSpace min_content() noexcept;
    [[nodiscard]] static AvailableSpace max_content() noexcept;
    [[nodiscard]] std::optional<Unit> definite_value() const noexcept;
    bool operator==(const AvailableSpace&) const = default;
};

} // namespace axisly::css
