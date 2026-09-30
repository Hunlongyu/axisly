#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace axisly::css::detail {

// Node identities are process-wide monotonic integers. A flat directory gives
// them direct bucket access; separately owned nodes retain stable addresses
// across growth and deletion. Removed identities are never reused.
template<class Node>
class NodeStore {
    struct Slot {
        std::uint64_t id{};
        std::unique_ptr<Node> node;
    };
    std::vector<Slot> slots_;
    std::size_t size_{};
    unsigned shift_{60};

    [[nodiscard]] static std::size_t bucket(std::uint64_t id, unsigned shift) noexcept {
        // High product bits also disperse identities interleaved across trees.
        return static_cast<std::size_t>((id * UINT64_C(11400714819323198485)) >> shift);
    }

    void grow() {
        const auto capacity = slots_.empty() ? std::size_t{16} : slots_.size() * 2;
        if (capacity < slots_.size() || capacity > slots_.max_size()) {
            throw std::length_error("CSS node directory capacity exceeded");
        }
        std::vector<Slot> grown(capacity);
        const auto mask = capacity - 1;
        const auto shift = 64u - static_cast<unsigned>(std::countr_zero(capacity));
        for (auto& slot : slots_) {
            if (!slot.id) continue;
            auto index = bucket(slot.id, shift);
            while (grown[index].id) index = (index + 1) & mask;
            grown[index] = std::move(slot);
        }
        slots_ = std::move(grown);
        shift_ = shift;
    }

public:
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] const Node* find(std::uint64_t id) const noexcept {
        if (!id || slots_.empty()) return nullptr;
        const auto mask = slots_.size() - 1;
        auto index = bucket(id, shift_);
        while (slots_[index].id) {
            if (slots_[index].id == id) return slots_[index].node.get();
            index = (index + 1) & mask;
        }
        return nullptr;
    }

    [[nodiscard]] Node* find(std::uint64_t id) noexcept {
        return const_cast<Node*>(std::as_const(*this).find(id));
    }

    void insert(std::uint64_t id, Node node) {
        if (!id || find(id)) throw std::logic_error("Duplicate CSS node identity");
        auto owned = std::make_unique<Node>(std::move(node));
        if (size_ >= slots_.size() / 2) grow();
        const auto mask = slots_.size() - 1;
        auto index = bucket(id, shift_);
        while (slots_[index].id) index = (index + 1) & mask;
        slots_[index] = Slot{id, std::move(owned)};
        ++size_;
    }

    void erase(std::uint64_t id) noexcept {
        if (!id || slots_.empty()) return;
        const auto mask = slots_.size() - 1;
        auto hole = bucket(id, shift_);
        while (slots_[hole].id && slots_[hole].id != id) hole = (hole + 1) & mask;
        if (!slots_[hole].id) return;
        slots_[hole] = {};
        --size_;
        // Back-shift the probe chain so deleted buckets need no tombstones.
        for (auto next = (hole + 1) & mask; slots_[next].id; next = (next + 1) & mask) {
            const auto home = bucket(slots_[next].id, shift_);
            if (((hole - home) & mask) < ((next - home) & mask)) {
                slots_[hole] = std::move(slots_[next]);
                slots_[next] = {};
                hole = next;
            }
        }
    }
};

} // namespace axisly::css::detail
