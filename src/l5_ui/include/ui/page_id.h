// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"
#include "ui/types.h"

namespace mister::ui {

enum class PageId : std::uint8_t {
    None = 0,

    Info,
    Progress,
    Confirm,
    Unlock,
    SelectIni,
    Browser,
    Recents,
    Cheats,
    About,

    System,
    Common,
    Misc,
    VideoProc,
    Uart,
    UartMode,
    UartBaud,
    ScriptRun,
    FbTerminal,

    CoreOptions,
    ArcadeDip,

    CoreSettings,
    ConfigSlots,

    JoyMap,
    KbdMap,
    LightGunCal,
    AdvancedMapList,
    AdvancedMapEdit,
    AdvancedMapButtons,
    AdvancedMapCapture,

    Mt32Pi,

    HdMenu,
    HdCoreSettings,
    HdCoreAbout,
    HdSettings,
    HdControllers,
    HdDisplay,
    HdAbout,
    HdMedia,
    HdSlotSheet,
    HdRecentGames,

    kCount,
};

[[nodiscard]] constexpr ItemId item_pushing(PageId page, ItemId focus) noexcept {
    constexpr unsigned kPageShift = 8;
    constexpr unsigned kFocusMask = 0xFFu;
    return ItemId{static_cast<std::uint16_t>(
        (static_cast<unsigned>(page) << kPageShift) | (focus.v & kFocusMask)
    )};
}

inline constexpr ItemId kMiscStorageRow = item_row(RowIndex{3});

static_assert(
    static_cast<unsigned>(PageId::kCount) == 41,
    "30 stock pages, ten pages with no stock state, and the closed sentinel. "
    "This pins the CARDINALITY only; the map itself — every ordinal, "
    "every New home — is joined by a check."
);

}
