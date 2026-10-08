#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <vector>

namespace axisly::css::detail {

// Nested queries borrow separate buffers; siblings reuse released storage.
// A workspace clears all live layout state before returning to this pool.
template<class Workspace>
class WorkspacePool {
public:
    explicit WorkspacePool(std::pmr::memory_resource* memory) : memory_(memory) {}
    class Lease {
    public:
        explicit Lease(WorkspacePool& pool) : pool_(pool), workspace_(pool.acquire()) {}
        ~Lease() { pool_.release(workspace_); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        [[nodiscard]] Workspace& get() noexcept { return workspace_; }
    private:
        WorkspacePool& pool_;
        Workspace& workspace_;
    };
private:
    std::vector<std::unique_ptr<Workspace>> workspaces_;
    std::size_t active_{};
    std::pmr::memory_resource* memory_;
    Workspace& acquire() {
        if (active_ == workspaces_.size()) workspaces_.push_back(std::make_unique<Workspace>(memory_));
        return *workspaces_[active_++];
    }
    void release(Workspace& workspace) noexcept {
        assert(active_ && workspaces_[active_ - 1].get() == &workspace);
        workspace.clear();
        --active_;
    }
};

} // namespace axisly::css::detail
