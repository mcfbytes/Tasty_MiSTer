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

    TextEntry,
    Network,
    WifiNetworks,
    WifiSaved,
    WifiAdvanced,
    CountryPick,
    Bluetooth,
    BtPair,
    BtDevices,

    kCount,
};

[[nodiscard]] constexpr RowKey push_row_key(PageId page) noexcept {
    constexpr unsigned kPageShift = 8;
    return RowKey::in_space(
        RowKeySpace::Push,
        RowKey::Ordinal{static_cast<std::uint16_t>(static_cast<unsigned>(page) << kPageShift)}
    );
}

static_assert(
    static_cast<unsigned>(PageId::kCount) == 50,
    "30 stock pages, nineteen pages with no stock state, and the closed sentinel. "
    "This pins the CARDINALITY only; the map itself — every ordinal, "
    "every New home — is joined by a check."
);

}
