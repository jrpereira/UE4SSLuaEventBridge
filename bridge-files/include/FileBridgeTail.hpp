#pragma once

// Tail delivery state (API.md "File sockets: Tail"): line splitting, held
// partial lines, 1 MiB pieces, and reset after truncation or rotation.

#include <FileBridgeNativeContract.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UE4SSLuaFileBridge::Core
{
struct TailDelivery
{
    std::string text;
    int64_t offset{};
    int64_t flags{};
};

class TailState
{
public:
    explicit TailState(bool chunks, std::size_t max_line = static_cast<std::size_t>(NativeContract::tail_max_line_bytes))
        : chunks_(chunks), max_line_(max_line)
    {
    }

    // Next byte offset to read from the file.
    [[nodiscard]] int64_t read_offset() const { return read_offset_; }

    // Start position without a reset flag (subscribe, first appearance).
    void start_at(int64_t offset)
    {
        read_offset_ = offset;
        held_.clear();
        held_offset_ = offset;
    }

    // Truncation or rotation: back to 0, drop the held partial line, flag the
    // next delivery.
    void reset()
    {
        start_at(0);
        pending_reset_ = true;
    }

    // Size observed on the file: shorter than the read position means
    // truncated. Returns true if it reset.
    bool observe_size(int64_t size)
    {
        if (size < read_offset_)
        {
            reset();
            return true;
        }
        return false;
    }

    // `bytes` were read at read_offset(); appends complete deliveries to `out`.
    void feed(std::string_view bytes, std::vector<TailDelivery>& out)
    {
        if (bytes.empty()) return;
        if (chunks_)
        {
            emit(out, std::string(bytes), read_offset_, 0);
            read_offset_ += static_cast<int64_t>(bytes.size());
            held_offset_ = read_offset_;
            return;
        }
        std::size_t start = 0;
        while (start < bytes.size())
        {
            const auto newline = bytes.find('\n', start);
            if (newline == std::string_view::npos)
            {
                append_held(bytes.substr(start), out);
                break;
            }
            append_held(bytes.substr(start, newline - start), out);
            std::string line = std::move(held_);
            held_.clear();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            emit(out, std::move(line), held_offset_, 0);
            held_offset_ = read_offset_ + static_cast<int64_t>(newline) + 1;
            start = newline + 1;
        }
        read_offset_ += static_cast<int64_t>(bytes.size());
    }

    [[nodiscard]] std::size_t held_bytes() const { return held_.size(); }

private:
    bool chunks_;
    std::size_t max_line_;
    int64_t read_offset_{};
    int64_t held_offset_{};
    std::string held_;
    bool pending_reset_{false};

    void emit(std::vector<TailDelivery>& out, std::string text, int64_t offset, int64_t flags)
    {
        if (pending_reset_)
        {
            flags |= NativeContract::DeliveryFlag::reset;
            pending_reset_ = false;
        }
        out.push_back(TailDelivery{std::move(text), offset, flags});
    }

    void append_held(std::string_view part, std::vector<TailDelivery>& out)
    {
        while (!part.empty())
        {
            const auto room = max_line_ - held_.size();
            const auto take = part.size() < room ? part.size() : room;
            held_.append(part.substr(0, take));
            part.remove_prefix(take);
            if (held_.size() == max_line_ && !part.empty())
            {
                // More of this line follows: deliver a full piece now.
                const auto length = static_cast<int64_t>(held_.size());
                emit(out, std::move(held_), held_offset_, NativeContract::DeliveryFlag::partial);
                held_.clear();
                held_offset_ += length;
            }
        }
    }
};
}
