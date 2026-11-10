// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/indexed_array.h"
#include "svc/types.h"

namespace mister::svc {

struct InputServiceCounts {
    Tally mapped{};
    Tally slotted{};
    Tally mask_recomputes{};
    Tally axis_edges{};
    Tally mouse_remainder_bytes{};
    Tally key_overflows{};
    Tally quirk_drops{};
    Tally grab_failures{};
    PlayerMask live{};
    infra::IndexedArray<KeyHash, kMaxPlayers, PlayerIndex> slot_hash{};
    Tally ghost_slots{};

    [[nodiscard]] KeyHash slot_key_hash(PlayerIndex player) const noexcept {
        const auto h = slot_hash.find(player);
        return h ? *h : KeyHash{};
    }
};

[[nodiscard]] constexpr std::array<KeyHash, kMaxPlayers> printed_hashes(
    const infra::IndexedArray<KeyHash, kMaxPlayers, PlayerIndex>& hashes
) noexcept {
    std::array<KeyHash, kMaxPlayers> out{};
    for (std::size_t i = 0; i < kMaxPlayers; ++i)
        out[i] = hashes[PlayerIndex::of(i)];
    return out;
}

}
