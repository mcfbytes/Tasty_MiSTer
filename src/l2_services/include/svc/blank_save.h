// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

#include "infra/byte_view.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/blank_region.h"

namespace mister::svc {

struct BlankSaveSpec {
    TASTY_SEAT_EXEMPT(const_shared);

    static constexpr proto::BlockSize kDefaultLbaBytes{512};

    static constexpr std::byte kErased{0xFF};

    std::byte fill{};
    std::span<const std::byte> header;
    std::span<const BlankRegion> regions{};

    bool generic_ff = false;
    proto::BlockSize lba_bytes = kDefaultLbaBytes;

    constexpr bool declared() const noexcept { return !header.empty() || !regions.empty(); }
};

inline void render_blank(
    const BlankSaveSpec& spec,
    proto::FileSize base,
    std::span<std::uint8_t> out
) {
    if (out.empty())
        return;
    std::ranges::fill(infra::byte_cast<std::byte>(out), spec.fill);
    BlankRegion{.block = {}, .within = {}, .bytes = spec.header}
        .paste_into(spec.lba_bytes, base, out);
    for (const BlankRegion& r : spec.regions)
        r.paste_into(spec.lba_bytes, base, out);
}

inline void fill_absent(
    infra::OptRef<const BlankSaveSpec> spec,
    proto::Lba lba,
    std::span<std::uint8_t> out
) noexcept {
    if (spec && spec->declared()) {
        render_blank(*spec, proto::byte_offset(lba, spec->lba_bytes), out);
        return;
    }
    std::ranges::fill(infra::byte_cast<std::byte>(out), BlankSaveSpec::kErased);
}

}
