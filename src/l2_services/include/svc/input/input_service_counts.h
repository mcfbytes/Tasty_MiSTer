// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/indexed_array.h"
#include "infra/tally.h"
#include "svc/types.h"

namespace mister::svc::input {

struct InputServiceCounts {
    infra::Tally mapped{};
    infra::Tally slotted{};
    infra::Tally mask_recomputes{};
    infra::Tally axis_edges{};
    infra::Tally mouse_remainder_bytes{};
    infra::Tally key_overflows{};
    infra::Tally quirk_drops{};
    infra::Tally grab_failures{};
    PlayerMask live{};
    infra::IndexedArray<KeyHash, kMaxPlayers, PlayerIndex> slot_hash{};
    infra::Tally ghost_slots{};

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
        out[i] = hashes[PlayerIndex::truncating(i)];
    return out;
}

}
