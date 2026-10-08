#include "../../src/css/node_store.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct ConstructionFailure {};

// Exercise storage ownership independently of Style allocation. Overalignment
// and throwing constructors must work at both new-page and recycled slots.
struct alignas(64) TrackedNode {
    struct Identity { std::uint64_t value; } id{};
    int* live;
    int value;
    std::byte payload[64 - sizeof(Identity) - sizeof(int*) - sizeof(int)];
    TrackedNode(int& count, int payload, bool reject)
        : live(&count), value(payload) {
        if (reject) throw ConstructionFailure{};
        ++*live;
    }
    ~TrackedNode() { --*live; }
};

void storage_lifetime() {
    int live = 0;
    {
        axisly::css::detail::NodeStore<TrackedNode> nodes;
        std::vector<std::uint64_t> ids, removed;
        const auto reject = [&] {
            const auto size = nodes.size();
            try {
                (void)nodes.emplace(live, -1, true);
            } catch (const ConstructionFailure&) {
                require(nodes.size() == size && live == static_cast<int>(size),
                    "Failed construction must preserve occupied slots and object lifetimes");
                return;
            }
            throw std::runtime_error("Expected construction failure");
        };
        for (int i = 0; i < 64; ++i) {
            reject();
            const auto id = nodes.emplace(live, i, false);
            ids.push_back(id);
            const auto* node = nodes.find(id);
            require(node && node->id.value == id && node->value == i,
                "Retry after construction failure must initialize the requested node");
            require(reinterpret_cast<std::uintptr_t>(node) % alignof(TrackedNode) == 0,
                "Page storage must preserve node alignment");
        }
        const auto* anchor = nodes.find(ids.front());
        for (std::size_t i = 1; i < ids.size(); i += 3) {
            nodes.erase(ids[i]);
            removed.push_back(ids[i]);
            require(!nodes.find(ids[i]), "Erased identity must not address a live node");
        }
        for (std::size_t i = 0; i < removed.size(); ++i) {
            reject();
            const auto id = nodes.emplace(live, 100, false);
            require(nodes.find(id) && nodes.find(id)->value == 100,
                "Rejected recycled construction must leave the free chain usable");
        }
        for (const auto id : removed) require(!nodes.find(id), "Recycling must not revive stale identities");
        require(nodes.find(ids.front()) == anchor && anchor->value == 0
            && nodes.size() == ids.size() && live == static_cast<int>(ids.size()),
            "Growth and failed recycled insertions must preserve surviving addresses and counts");
    }
    require(live == 0, "Destroying the store must release each constructed node exactly once");
}
} // namespace

int main() {
    try {
        storage_lifetime();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
