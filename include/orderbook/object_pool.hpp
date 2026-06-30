#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace ob {

// A free-list object pool. The whole point of a matching engine is to never
// call into the system allocator on the hot path -- malloc/free latency is
// unbounded and cache-hostile. We pre-grow a backing store and hand out / take
// back T* from a free list instead.
//
// std::deque is used as the backing store specifically because it guarantees
// reference/pointer stability across growth (unlike std::vector, which would
// invalidate every outstanding Order* on reallocation).
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t reserve = 4096) {
        free_list_.reserve(reserve);
    }

    // O(1). Reuses a freed slot if available, otherwise grows the store.
    [[nodiscard]] T* acquire() {
        if (!free_list_.empty()) {
            T* p = free_list_.back();
            free_list_.pop_back();
            return p;
        }
        return &storage_.emplace_back();
    }

    // O(1). Returns the slot to the free list; the object is not destroyed,
    // it is simply made available for the next acquire().
    void release(T* p) noexcept { free_list_.push_back(p); }

    [[nodiscard]] std::size_t in_use() const noexcept {
        return storage_.size() - free_list_.size();
    }
    [[nodiscard]] std::size_t capacity() const noexcept {
        return storage_.size();
    }

private:
    std::deque<T> storage_;
    std::vector<T*> free_list_;
};

} // namespace ob
