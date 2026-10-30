// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::app {

enum class RungSlot : std::uint8_t {
    BootLadder,
    SavestateHold,
    SaveMount,
    AddonsAfter,
    Addons,
    Watch,
    Pieces,
    Load,
    Manifest,
    Walk,
    Companion,
    kCount
};

enum class RungSource : std::uint8_t { Role, Held };

struct RungRow {
    RungSlot slot;
    RungSource source;
};

inline constexpr auto kRungRows = std::to_array<RungRow>({
    {RungSlot::BootLadder, RungSource::Held},
    {RungSlot::SavestateHold, RungSource::Held},
    {RungSlot::SaveMount, RungSource::Held},
    {RungSlot::AddonsAfter, RungSource::Held},
    {RungSlot::Addons, RungSource::Role},
    {RungSlot::Watch, RungSource::Held},
    {RungSlot::Pieces, RungSource::Role},
    {RungSlot::Load, RungSource::Role},
    {RungSlot::Manifest, RungSource::Role},
    {RungSlot::Walk, RungSource::Role},
    {RungSlot::Companion, RungSource::Role},
});

consteval bool rung_rows_cover_each_slot_once() {
    unsigned seen[static_cast<std::size_t>(RungSlot::kCount)] = {};
    for (const RungRow& r : kRungRows)
        ++seen[static_cast<std::size_t>(r.slot)];
    for (unsigned n : seen) {
        if (n != 1)
            return false;
    }
    return true;
}

static_assert(
    rung_rows_cover_each_slot_once(),
    "kRungRows declares each rung source exactly once - a slot with no row is "
    "never stepped, a slot with two is stepped twice a pass"
);

}
