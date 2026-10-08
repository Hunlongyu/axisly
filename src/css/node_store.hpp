#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <utility>
#include <vector>

namespace axisly::css::detail {

[[nodiscard]] std::uint32_t allocate_node_epoch();

// Identities address slots directly. Fixed pages keep node/style/cache
// addresses stable while amortizing storage allocation across several nodes.
// Only occupied slots construct a Node; a small tree does not initialize an
// entire page of styles and caches. Deleted slots are reused with a fresh,
// process-unique epoch so stale and foreign identities never alias live nodes.
template<class Node>
class NodeStore {
    static constexpr auto no_slot = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t page_shift = 4;
    static constexpr std::size_t page_size = std::size_t{1} << page_shift;
    struct Slot {
        std::uint32_t epoch{};
        std::uint32_t next_free{no_slot};
        union { Node node; };
        Slot() noexcept {}
        ~Slot() { if (epoch) std::destroy_at(&node); }
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
    };
    struct Page {
        explicit Page(std::pmr::memory_resource* memory) : allocator(memory) {}
        Page(const Page&) = delete;
        Page& operator=(const Page&) = delete;
        std::pmr::polymorphic_allocator<Slot> allocator;
        Slot* slots{allocator.allocate(page_size)};
        std::size_t used{};
        ~Page() {
            for (std::size_t i = 0; i < used; ++i) std::destroy_at(slots + i);
            allocator.deallocate(slots, page_size);
        }
    };
    std::vector<std::unique_ptr<Page>> pages_;
    std::pmr::memory_resource* memory_;
    std::size_t slot_count_{}, size_{};
    std::uint32_t epoch_{allocate_node_epoch()};
    std::uint32_t free_{no_slot};

    [[nodiscard]] Slot& slot(std::uint32_t index) noexcept {
        return pages_[index >> page_shift]->slots[index & (page_size - 1)];
    }
    [[nodiscard]] const Slot& slot(std::uint32_t index) const noexcept {
        return pages_[index >> page_shift]->slots[index & (page_size - 1)];
    }

public:
    [[nodiscard]] static constexpr std::uint32_t slot_index(std::uint64_t id) noexcept {
        return static_cast<std::uint32_t>(id) - std::uint32_t{1};
    }
    explicit NodeStore(std::pmr::memory_resource* memory = std::pmr::new_delete_resource())
        : memory_(memory) {}
    NodeStore(const NodeStore&) = delete;
    NodeStore& operator=(const NodeStore&) = delete;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    template<class Visitor>
    void for_each(Visitor&& visitor) {
        for (std::size_t i = 0; i < slot_count_; ++i) {
            auto& entry = slot(static_cast<std::uint32_t>(i));
            if (entry.epoch) visitor(entry.node);
        }
    }

    [[nodiscard]] const Node* find(std::uint64_t id) const noexcept {
        const auto index = slot_index(id);
        const auto epoch = static_cast<std::uint32_t>(id >> 32);
        if (!epoch || index >= slot_count_) return nullptr;
        const auto& found = slot(index);
        return found.epoch == epoch ? &found.node : nullptr;
    }

    [[nodiscard]] Node* find(std::uint64_t id) noexcept {
        return const_cast<Node*>(std::as_const(*this).find(id));
    }

    template<class... Args>
    [[nodiscard]] std::uint64_t emplace(Args&&... args) {
        const bool reuse = free_ != no_slot;
        if (!reuse && slot_count_ >= no_slot) throw std::length_error("CSS node slot capacity exceeded");
        const auto index = reuse ? free_ : static_cast<std::uint32_t>(slot_count_);
        const auto epoch = reuse ? allocate_node_epoch() : epoch_;
        if (!reuse && index % page_size == 0) pages_.push_back(std::make_unique<Page>(memory_));
        auto& destination = slot(index);
        if (!reuse) std::construct_at(&destination);
        const auto next = destination.next_free;
        const auto id = (static_cast<std::uint64_t>(epoch) << 32)
            | (static_cast<std::uint64_t>(index) + 1);
        try {
            std::construct_at(&destination.node, std::forward<Args>(args)...);
            destination.node.id = {id};
        } catch (...) {
            if (!reuse) {
                std::destroy_at(&destination);
                // A newly allocated but unused page can be retried normally.
                if (index % page_size == 0) pages_.pop_back();
            }
            throw;
        }
        destination.epoch = epoch;
        if (reuse) {
            free_ = next;
            epoch_ = epoch;
        } else {
            ++pages_.back()->used;
            ++slot_count_;
        }
        ++size_;
        return id;
    }

    void erase(std::uint64_t id) noexcept {
        const auto index = slot_index(id);
        const auto epoch = static_cast<std::uint32_t>(id >> 32);
        if (!epoch || index >= slot_count_) return;
        auto& removed = slot(index);
        if (removed.epoch != epoch) return;
        std::destroy_at(&removed.node);
        removed.epoch = 0;
        removed.next_free = free_;
        free_ = index;
        --size_;
    }
};

} // namespace axisly::css::detail
