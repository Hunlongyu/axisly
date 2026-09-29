#include <axisly/css_values.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace axisly::css {
namespace {
Unit finite(Unit value) {
    if (!std::isfinite(value)) throw std::invalid_argument("CSS numeric value must be finite");
    return value;
}

std::optional<Unit> checked(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<Unit>::max()) {
        throw std::overflow_error("CSS calculation exceeds Unit range");
    }
    return static_cast<Unit>(value);
}

void validate_basis(std::optional<Unit> basis) {
    if (basis && (!std::isfinite(*basis) || *basis < 0)) {
        throw std::invalid_argument("CSS percentage basis must be finite and nonnegative");
    }
}
} // namespace

struct Calculation::Node {
    enum class Operation : std::uint8_t { Length, Percent, Add, Subtract, Scale, Min, Max, Clamp };
    Operation operation;
    Unit value{};
    std::shared_ptr<const Node> first;
    std::shared_ptr<const Node> second;
    std::shared_ptr<const Node> third;
    std::uint8_t depth{1};
};

Calculation::Calculation(std::shared_ptr<const Node> root) noexcept : root_(std::move(root)) {}

Calculation Calculation::length(Unit value) {
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Length, finite(value)}));
}

Calculation Calculation::percent(Unit fraction) {
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Percent, finite(fraction)}));
}

namespace {
std::uint8_t next_depth(std::uint8_t first, std::uint8_t second = 0, std::uint8_t third = 0) {
    const auto depth = static_cast<unsigned>(std::max({first, second, third})) + 1;
    if (depth > 64) throw std::invalid_argument("CSS calculation depth exceeds 64");
    return static_cast<std::uint8_t>(depth);
}
} // namespace

Calculation Calculation::add(Calculation left, Calculation right) {
    const auto depth = next_depth(left.root_->depth, right.root_->depth);
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Add, 0, std::move(left.root_),
        std::move(right.root_), nullptr, depth}));
}

Calculation Calculation::subtract(Calculation left, Calculation right) {
    const auto depth = next_depth(left.root_->depth, right.root_->depth);
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Subtract, 0, std::move(left.root_),
        std::move(right.root_), nullptr, depth}));
}

Calculation Calculation::scale(Calculation value, Unit factor) {
    const auto depth = next_depth(value.root_->depth);
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Scale, finite(factor),
        std::move(value.root_), nullptr, nullptr, depth}));
}

Calculation Calculation::minimum(Calculation left, Calculation right) {
    const auto depth = next_depth(left.root_->depth, right.root_->depth);
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Min, 0, std::move(left.root_),
        std::move(right.root_), nullptr, depth}));
}

Calculation Calculation::maximum(Calculation left, Calculation right) {
    const auto depth = next_depth(left.root_->depth, right.root_->depth);
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Max, 0, std::move(left.root_),
        std::move(right.root_), nullptr, depth}));
}

Calculation Calculation::clamp(Calculation lower, Calculation value, Calculation upper) {
    const auto depth = next_depth(lower.root_->depth, value.root_->depth, upper.root_->depth);
    return Calculation(std::make_shared<Node>(Node{Node::Operation::Clamp, 0, std::move(lower.root_),
        std::move(value.root_), std::move(upper.root_), depth}));
}

std::optional<Unit> Calculation::resolve(std::optional<Unit> basis) const {
    validate_basis(basis);
    // Keep intermediate arithmetic in double. A final finite result must not
    // overflow merely because a subexpression exceeded the float range.
    const auto evaluate = [&](const auto& self, const Node& node) -> std::optional<double> {
        using Op = Node::Operation;
        if (node.operation == Op::Length) return static_cast<double>(node.value);
        if (node.operation == Op::Percent) {
            if (!basis) return std::nullopt;
            return static_cast<double>(node.value) * *basis;
        }
        const auto first = self(self, *node.first);
        if (!first) return std::nullopt;
        if (node.operation == Op::Scale) return *first * node.value;
        const auto second = self(self, *node.second);
        if (!second) return std::nullopt;
        switch (node.operation) {
        case Op::Add: return *first + *second;
        case Op::Subtract: return *first - *second;
        case Op::Min: return std::min(*first, *second);
        case Op::Max: return std::max(*first, *second);
        case Op::Clamp: {
            const auto third = self(self, *node.third);
            if (!third) return std::nullopt;
            return std::max(*first, std::min(*second, *third));
        }
        default: throw std::logic_error("Invalid CSS calculation node");
        }
    };
    const auto result = evaluate(evaluate, *root_);
    return result ? checked(*result) : std::nullopt;
}

bool Calculation::operator==(const Calculation& other) const noexcept {
    const auto equals = [&](const auto& self, const std::shared_ptr<const Node>& left,
                            const std::shared_ptr<const Node>& right) -> bool {
        if (left == right) return true;
        if (!left || !right || left->operation != right->operation || left->value != right->value) return false;
        return self(self, left->first, right->first) && self(self, left->second, right->second)
            && self(self, left->third, right->third);
    };
    return equals(equals, root_, other.root_);
}

Length::Length(LengthKind kind, Unit value) noexcept : kind_(kind), value_(value) {}

Length Length::length(Unit value) { return Length(LengthKind::Length, finite(value)); }
Length Length::percent(Unit fraction) { return Length(LengthKind::Percent, finite(fraction)); }
Length Length::automatic() noexcept { return Length(LengthKind::Auto); }
Length Length::min_content() noexcept { return Length(LengthKind::MinContent); }
Length Length::max_content() noexcept { return Length(LengthKind::MaxContent); }
Length Length::fit_content() noexcept { return Length(LengthKind::FitContent); }
Length Length::stretch() noexcept { return Length(LengthKind::Stretch); }
Length Length::content() noexcept { return Length(LengthKind::Content); }
Length Length::fit_content_length(Unit limit) { return Length(LengthKind::FitContentLength, finite(limit)); }
Length Length::fit_content_percent(Unit fraction) { return Length(LengthKind::FitContentPercent, finite(fraction)); }
Length Length::calc(Calculation expression) {
    Length result(LengthKind::Calc);
    result.calculation_ = std::move(expression);
    return result;
}

std::optional<Unit> Length::resolve_numeric(std::optional<Unit> basis) const {
    validate_basis(basis);
    switch (kind_) {
    case LengthKind::Length: return value_;
    case LengthKind::Percent:
        return basis ? checked(static_cast<double>(value_) * *basis) : std::nullopt;
    // Taffy's opaque calc callback is invoked only after its containing basis
    // becomes definite, even when the expression contains only fixed lengths.
    case LengthKind::Calc: return basis ? calculation_->resolve(basis) : std::nullopt;
    default: return std::nullopt;
    }
}

std::optional<Unit> Length::resolve_fit_content_limit(std::optional<Unit> basis) const {
    validate_basis(basis);
    switch (kind_) {
    case LengthKind::FitContentLength: return value_;
    case LengthKind::FitContentPercent:
        return basis ? checked(static_cast<double>(value_) * *basis) : std::nullopt;
    default: return std::nullopt;
    }
}

bool Length::operator==(const Length& other) const noexcept {
    return kind_ == other.kind_ && value_ == other.value_ && calculation_ == other.calculation_;
}

AvailableSpace AvailableSpace::definite(Unit value) {
    if (finite(value) < 0) throw std::invalid_argument("Definite space must be nonnegative");
    return {AvailableKind::Definite, value};
}
AvailableSpace AvailableSpace::indefinite() noexcept { return {AvailableKind::Indefinite, 0}; }
AvailableSpace AvailableSpace::min_content() noexcept { return {AvailableKind::MinContent, 0}; }
AvailableSpace AvailableSpace::max_content() noexcept { return {AvailableKind::MaxContent, 0}; }
std::optional<Unit> AvailableSpace::definite_value() const noexcept {
    return kind == AvailableKind::Definite ? std::optional<Unit>(value) : std::nullopt;
}

} // namespace axisly::css
