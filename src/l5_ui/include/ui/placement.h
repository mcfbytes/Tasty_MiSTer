// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ui/page_id.h"

namespace mister::ui {

enum class Placement : std::uint8_t {
    Slot,
    ResetGame,
    Cheats,
    SaveState,
    LoadState,
    CoreSettings,
    CoreAbout,
    Hidden,
    kCount,
};

struct PlacementHome {
    Placement placement;
    PageId host;
    PageId hub;
};

inline constexpr std::array<PlacementHome, static_cast<std::size_t>(Placement::kCount)>
    kPlacementHome{{
        {Placement::Slot, PageId::HdMedia, PageId::HdMenu},
        {Placement::ResetGame, PageId::HdMenu, PageId::HdMenu},
        {Placement::Cheats, PageId::Cheats, PageId::HdMenu},
        {Placement::SaveState, PageId::HdMenu, PageId::HdMenu},
        {Placement::LoadState, PageId::HdMenu, PageId::HdMenu},
        {Placement::CoreSettings, PageId::HdCoreSettings, PageId::HdMenu},
        {Placement::CoreAbout, PageId::HdCoreAbout, PageId::HdCoreSettings},
        {Placement::Hidden, PageId::None, PageId::None},
    }};

consteval bool placement_homes_are_indexed_by_placement() {
    for (std::size_t i = 0; i < kPlacementHome.size(); ++i) {
        if (static_cast<std::size_t>(kPlacementHome[i].placement) != i)
            return false;
    }
    return true;
}

static_assert(placement_homes_are_indexed_by_placement());

constexpr PageId host_of(Placement p) noexcept {
    return kPlacementHome[static_cast<std::size_t>(p)].host;
}

}
