// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

template <std::uint16_t Slots, std::size_t Bytes>
class LinkBytes {
    TASTY_SEAT_EXEMPT(component);
    static_assert(Slots >= 2, "slot 0 is the empty id; at least one slot must carry bytes");

public:
    static constexpr std::uint16_t kSlots = Slots;
    static constexpr std::uint16_t kUsable = Slots - 1;
    static constexpr std::size_t kBytes = Bytes;
    static constexpr std::uint16_t kNone = 0;

    static constexpr std::uint32_t kHold = 2;

    [[nodiscard]] Ex<std::uint16_t> intern(std::span<const std::uint8_t> bytes, std::uint32_t at,
                                           std::uint32_t popped) noexcept {
        if (bytes.empty() || bytes.size() > kBytes) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(bytes.size())});
        }
        const std::uint16_t id = next_;
        if (!free_(id, popped)) {
            return std::unexpected(Error{Errc::would_block, ERR_SITE(), id});
        }
        std::memcpy(storage_[id], bytes.data(), bytes.size());
        lengths_[id] = static_cast<std::uint16_t>(bytes.size());
        named_at_[id] = at;
        live_[id] = true;
        next_ = static_cast<std::uint16_t>(id + 1 >= kSlots ? 1 : id + 1);
        return id;
    }

    [[nodiscard]] bool would_accept(std::size_t bytes, unsigned chunks,
                                    std::uint32_t popped) const noexcept {
        if (chunks == 0) return true;
        if (bytes == 0 || bytes > kBytes) return false;
        std::uint16_t id = next_;
        for (unsigned i = 0; i < chunks; ++i) {
            if (!free_(id, popped)) return false;
            id = static_cast<std::uint16_t>(id + 1 >= kSlots ? 1 : id + 1);
        }
        return true;
    }

    [[nodiscard]] std::span<const std::uint8_t> get(std::uint16_t id) const noexcept {
        if (id == kNone || id >= kSlots) return {};
        return {storage_[id], lengths_[id]};
    }

private:
    [[nodiscard]] bool free_(std::uint16_t id, std::uint32_t popped) const noexcept {
        if (!live_[id]) return true;
        return static_cast<std::int32_t>(popped - named_at_[id]) >=
               static_cast<std::int32_t>(kHold);
    }

    std::uint8_t storage_[kSlots][kBytes]{};
    std::uint16_t lengths_[kSlots]{};
    std::uint32_t named_at_[kSlots]{};
    bool live_[kSlots]{};
    std::uint16_t next_ = 1;
};

}  // namespace mister::app
