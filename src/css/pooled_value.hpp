#pragma once

#include "storage_page.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace axisly::css::detail {

template<class T, bool Shared = true> class ValuePool;
template<bool Shared> struct ValueReferences {};
template<> struct ValueReferences<true> { std::size_t references{}; };
template<class T, bool Shared> struct ValueSlot : ValueReferences<Shared> {
    union { T value; ValueSlot* next; };
    ValuePool<T, Shared>* pool{};
    ValueSlot() noexcept : next(nullptr) {}
    ~ValueSlot() {}
};

// Values stay at stable addresses. Handles are confined to their owning tree,
// within non-concurrent compute/mutation; reference counts need no atomics.
// Resolved styles share immutable values. Working geometry has one owner;
// its cache takes a value snapshot instead of sharing a mutable root.
template<class T, bool Shared = true> class PooledValue {
    friend class ValuePool<T, Shared>;
public:
    PooledValue() = default;
    PooledValue(const PooledValue&) requires (!Shared) = delete;
    PooledValue& operator=(const PooledValue&) requires (!Shared) = delete;
    PooledValue(const PooledValue& other) noexcept requires Shared : slot_(other.slot_) {
        if (slot_) ++slot_->references;
    }
    PooledValue(PooledValue&& other) noexcept : slot_(std::exchange(other.slot_, nullptr)) {}
    PooledValue& operator=(const PooledValue& other) noexcept requires Shared {
        if (slot_ == other.slot_) return *this;
        release();
        slot_ = other.slot_;
        if (slot_) ++slot_->references;
        return *this;
    }
    PooledValue& operator=(PooledValue&& other) noexcept {
        if (this == &other) return *this;
        release();
        slot_ = std::exchange(other.slot_, nullptr);
        return *this;
    }
    ~PooledValue() { release(); }
    [[nodiscard]] explicit operator bool() const noexcept { return slot_ != nullptr; }
    [[nodiscard]] const T* get() const noexcept { return slot_ ? &slot_->value : nullptr; }
    [[nodiscard]] const T& read() const noexcept { return slot_->value; }
    [[nodiscard]] T& write() {
        if constexpr (Shared) {
            if (slot_->references != 1) *this = slot_->pool->create(slot_->value);
        }
        return slot_->value;
    }
    [[nodiscard]] T& operator*() { return write(); }
    [[nodiscard]] const T& operator*() const noexcept { return read(); }
private:
    ValueSlot<T, Shared>* slot_{};
    explicit PooledValue(ValueSlot<T, Shared>* slot) noexcept : slot_(slot) {}
    void release() noexcept {
        if (!slot_) return;
        if constexpr (Shared) {
            if (--slot_->references == 0) slot_->pool->release(slot_);
        } else slot_->pool->release(slot_);
    }
};

template<class T, bool Shared> class ValuePool {
    friend class PooledValue<T, Shared>;
    static_assert(std::is_nothrow_copy_constructible_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);
    static constexpr std::size_t maximum_page_size = 256;
public:
    explicit ValuePool(std::pmr::memory_resource* memory = std::pmr::new_delete_resource())
        : memory_(memory) {}
    [[nodiscard]] std::pmr::memory_resource* memory() const noexcept { return memory_; }
    ValuePool(const ValuePool&) = delete;
    ValuePool& operator=(const ValuePool&) = delete;
    ~ValuePool() { assert(live_ == 0); }

    [[nodiscard]] PooledValue<T, Shared> create(const T& value) {
        if (!free_) grow();
        auto* slot = free_;
        free_ = slot->next;
        std::construct_at(&slot->value, value);
        if constexpr (Shared) slot->references = 1;
#ifndef NDEBUG
        ++live_;
#endif
        return PooledValue<T, Shared>(slot);
    }
    void reserve_hint(std::size_t values) noexcept {
        if (pages_.empty()) next_page_size_ = std::clamp(values, std::size_t{16}, maximum_page_size);
    }
private:
    std::vector<StoragePage<ValueSlot<T, Shared>>> pages_;
    std::pmr::memory_resource* memory_;
    ValueSlot<T, Shared>* free_{};
#ifndef NDEBUG
    std::size_t live_{};
#endif
    std::size_t next_page_size_{16};

    void grow() {
        const auto count = next_page_size_;
        StoragePage<ValueSlot<T, Shared>> page(count, memory_);
        auto* storage = page.get();
        pages_.push_back(std::move(page));
        for (std::size_t i = 0; i < count; ++i) {
            auto& slot = storage[i];
            slot.pool = this;
            slot.next = free_;
            free_ = &slot;
        }
        next_page_size_ = std::min(maximum_page_size, count * 2);
    }
    void release(ValueSlot<T, Shared>* slot) noexcept {
        std::destroy_at(&slot->value);
        slot->next = free_;
        free_ = slot;
#ifndef NDEBUG
        --live_;
#endif
    }
};

} // namespace axisly::css::detail
