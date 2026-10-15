// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "app/ring_slab.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

template <std::uint16_t Slots, std::size_t Bytes, SeatTag Producer, SeatTag Consumer>
class LinkBytes {
    TASTY_SEAT_EXEMPT(component);
    static_assert(Slots >= 2, "slot 0 is the empty id; at least one slot must carry bytes");

public:
    static constexpr std::uint16_t kSlots = Slots;
    static constexpr std::uint16_t kUsable = Slots - 1;
    static constexpr std::size_t kBytes = Bytes;
    static constexpr std::uint16_t kNone = 0;
    static constexpr std::size_t kPool = std::bit_ceil(std::size_t{kUsable});

    struct Body {
        TASTY_SEAT_MEDIATOR(Any, Any);
        std::uint8_t data[Bytes];
        std::uint16_t len;
    };

    LinkBytes() noexcept : slab_(kUsable) {}
    LinkBytes(const LinkBytes&) = delete;
    LinkBytes& operator=(const LinkBytes&) = delete;

    [[nodiscard]] Ex<std::uint16_t> intern(std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.empty() || bytes.size() > kBytes) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(bytes.size())});
        }
        auto& p = slab_.producer();
        for (std::size_t n = 0; n < kPool && p.reap(); ++n) {
        }
        const auto i = p.stage();
        if (!i) return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
        const auto b = p.staged(*i);
        if (!b) return std::unexpected(Error{Errc::would_block, ERR_SITE(), *i});
        std::memcpy(b->data, bytes.data(), bytes.size());
        b->len = static_cast<std::uint16_t>(bytes.size());
        return static_cast<std::uint16_t>(*i + 1);
    }

    [[nodiscard]] bool would_accept(std::size_t bytes, unsigned chunks) const noexcept {
        if (chunks == 0) return true;
        if (bytes == 0 || bytes > kBytes) return false;
        return slab_.producer().free_now() >= chunks;
    }

    void send_staged(std::uint32_t at) noexcept { slab_.producer().send_staged(at); }

    void release_before(std::uint32_t q) noexcept { slab_.consumer().release_before(q); }
    void take_sent() noexcept { slab_.consumer().take_sent(); }

    [[nodiscard]] std::span<const std::uint8_t> get(std::uint16_t id) const noexcept {
        if (id == kNone || id > kUsable) return {};
        const auto b = slab_.consumer().held(static_cast<std::uint8_t>(id - 1));
        if (!b) return {};
        return {b->data, b->len};
    }

private:
    RingSlab<Body, kPool, Producer, Consumer> slab_;
};

}  // namespace mister::app
