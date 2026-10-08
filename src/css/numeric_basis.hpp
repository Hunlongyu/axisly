#pragma once

#include <axisly/css_values.hpp>

namespace axisly::css::detail {
// Validate a containing-block basis once, then use it for every dimension or
// edge in that resolution stage. Calc and percentage overflow checks remain
// in the common Length implementation.
class NumericBasis {
public:
    explicit NumericBasis(std::optional<Unit> value) : value_(value) {
        if (value && (!std::isfinite(*value) || *value < 0)) {
            invalid_numeric_value("CSS percentage basis must be finite and nonnegative");
        }
    }
    [[nodiscard]] std::optional<Unit> resolve(const Length& length) const {
        return length.resolve_numeric_validated(value_);
    }
    [[nodiscard]] static std::optional<Unit> resolve_indefinite(const Length& length) {
        // Percentages and calculations require a containing basis. Keeping
        // this operation explicit avoids carrying the general resolver's
        // checked multiplication and expression path into static boxes.
        if (length.kind() == LengthKind::Length) return length.numeric_value();
        return std::nullopt;
    }
private:
    std::optional<Unit> value_;
};

struct IndefiniteBasis {
    [[nodiscard]] std::optional<Unit> resolve(const Length& length) const {
        return NumericBasis::resolve_indefinite(length);
    }
};
} // namespace axisly::css::detail
