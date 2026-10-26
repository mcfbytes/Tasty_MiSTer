// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "cores/blank_region.h"
#include "infra/byte_view.h"
#include "infra/seat.h"

namespace mister::cores {

struct BlankSaveSpec {
    TASTY_SEAT_EXEMPT(const_shared);

    std::byte fill{};
    std::span<const std::byte> header;
    std::span<const BlankRegion> regions{};

    bool generic_ff = false;

    std::uint32_t lba_bytes = 512;

    constexpr bool declared() const noexcept { return !header.empty() || !regions.empty(); }
};

namespace detail {

inline void blank_paste(
    std::uint64_t base,
    std::span<std::uint8_t> out,
    std::uint64_t at,
    std::span<const std::byte> src
) {
    if (src.empty() || out.empty())
        return;
    const std::uint64_t win_end = base + out.size();
    const std::uint64_t src_end = at + src.size();
    if (src_end <= base || at >= win_end)
        return;
    const std::uint64_t from = at > base ? at : base;
    const std::uint64_t to = src_end < win_end ? src_end : win_end;
    std::memcpy(
        out.data() + (from - base),
        infra::byte_cast<const std::uint8_t>(src).data() + (from - at),
        static_cast<std::size_t>(to - from)
    );
}
}

inline void render_blank(
    const BlankSaveSpec& spec,
    std::uint64_t base_offset,
    std::span<std::uint8_t> out
) {
    if (out.empty())
        return;
    std::memset(out.data(), std::to_integer<int>(spec.fill), out.size());
    detail::blank_paste(base_offset, out, 0, spec.header);
    for (const BlankRegion& r : spec.regions) {
        detail::blank_paste(base_offset, out, r.offset_bytes, r.bytes);
    }
}

}
