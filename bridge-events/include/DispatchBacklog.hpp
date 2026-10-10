#pragma once
#include <stdexcept>
#include <utility>
#include <vector>

namespace UE4SSLuaEventBridge {
// Consumer-owned. A fresh producer batch can only replace an exhausted batch.
template<class T> class DispatchBacklog {
public:
    bool empty() const { return index_ == batch_.size(); }
    std::size_t size() const { return batch_.size() - index_; }
    T pop() {
        if (empty()) throw std::logic_error("empty dispatch backlog");
        return std::move(batch_[index_++]);
    }
    void load(std::vector<T> batch) {
        if (!empty()) throw std::logic_error("cannot replace pending events");
        batch_ = std::move(batch); index_ = 0;
    }
    std::vector<T> release_buffer() {
        if (!empty()) throw std::logic_error("cannot recycle pending events");
        index_ = 0;
        return std::exchange(batch_, {});
    }
private:
    std::vector<T> batch_;
    std::size_t index_{};
};
}
