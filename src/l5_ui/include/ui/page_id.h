// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

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
    AtariCart,

    CoreSettings,
    ConfigSlots,

    JoyMap,
    KbdMap,
    LightGunCal,
    AdvancedMapList,
    AdvancedMapEdit,
    AdvancedMapButtons,
    AdvancedMapCapture,

    MinimigMain,
    MinimigDisk,
    MinimigChipset,
    MinimigVideo,
    MinimigCd32Cdtv,
    MinimigLoadConfig,
    MinimigSaveConfig,
    ArchieMain,
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
static_assert(
    static_cast<unsigned>(PageId::kCount) == 50,
    "39 stock pages, ten pages with no stock state, and the closed sentinel. "
    "This pins the CARDINALITY only; the map itself — every ordinal, "
    "every New home — is joined by a check."
);

}
