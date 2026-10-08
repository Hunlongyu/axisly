#include <axisly/css_values.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace axisly::css {

[[noreturn]] void detail::invalid_numeric_value(const char* message) {
    throw std::invalid_argument(message);
}

[[noreturn]] void detail::invalid_definite_space(Unit value) {
    if (!std::isfinite(value)) invalid_numeric_value("CSS numeric value must be finite");
    invalid_numeric_value("Definite space must be nonnegative");
}

[[noreturn]] void detail::numeric_value_overflow(const char* message) {
    throw std::overflow_error(message);
}

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

struct alignas(16) Length::CalculationStorage {
    std::atomic<std::size_t> references{1};
    Calculation expression;

    explicit CalculationStorage(Calculation value) noexcept : expression(std::move(value)) {}
};

Length::CalculationStorage* Length::calculation_storage() const noexcept {
    return reinterpret_cast<CalculationStorage*>(static_cast<std::uintptr_t>(representation_ & ~kind_mask));
}

void Length::retain_calculation() const noexcept {
    calculation_storage()->references.fetch_add(1, std::memory_order_relaxed);
}

void Length::release_calculation() const noexcept {
    auto* storage = calculation_storage();
    if (storage->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete storage;
}

std::optional<Unit> Length::resolve_calculation(std::optional<Unit> basis) const {
    return calculation_storage()->expression.resolve(basis);
}

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
    auto* storage = new CalculationStorage(std::move(expression));
    Length result(LengthKind::Auto);
    result.representation_ = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(storage))
        | static_cast<std::uint64_t>(LengthKind::Calc);
    return result;
}

std::optional<Unit> Length::resolve_fit_content_limit(std::optional<Unit> basis) const {
    validate_basis(basis);
    switch (kind()) {
    case LengthKind::FitContentLength: return numeric_value();
    case LengthKind::FitContentPercent:
        return basis ? checked(static_cast<double>(numeric_value()) * *basis) : std::nullopt;
    default: return std::nullopt;
    }
}

bool Length::operator==(const Length& other) const noexcept {
    if (representation_ == other.representation_) return true;
    if (kind() != other.kind()) return false;
    if (kind() == LengthKind::Calc) {
        return calculation_storage()->expression == other.calculation_storage()->expression;
    }
    return numeric_value() == other.numeric_value();
}

} // namespace axisly::css
