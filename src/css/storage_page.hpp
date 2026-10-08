#pragma once

#include <memory>
#include <memory_resource>
#include <utility>

namespace axisly::css::detail {

// Fixed-size constructed storage with stable addresses and explicit resource
// ownership in the containing tree. Moving a page never moves its elements.
template<class T> class StoragePage {
public:
    StoragePage(std::size_t count, std::pmr::memory_resource* memory)
        : allocator_(memory), count_(count), data_(allocator_.allocate(count)) {
        try { std::uninitialized_default_construct_n(data_, count_); }
        catch (...) { allocator_.deallocate(data_, count_); throw; }
    }
    StoragePage(StoragePage&& other) noexcept
        : allocator_(other.allocator_), count_(other.count_), data_(std::exchange(other.data_, nullptr)) {}
    StoragePage(const StoragePage&) = delete;
    StoragePage& operator=(const StoragePage&) = delete;
    ~StoragePage() {
        if (!data_) return;
        std::destroy_n(data_, count_);
        allocator_.deallocate(data_, count_);
    }
    [[nodiscard]] T* get() const noexcept { return data_; }
private:
    std::pmr::polymorphic_allocator<T> allocator_;
    std::size_t count_;
    T* data_;
};

} // namespace axisly::css::detail
