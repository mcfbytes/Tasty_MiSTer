// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "ui/page_id.h"

namespace mister::ui {

enum class PageBuilder : std::uint8_t { None, MakePage, FreeFactory };

struct PageHome {
    PageId id;
    PageBuilder builder;
    std::optional<std::string_view> owner;
};

inline constexpr std::array<PageHome, static_cast<std::size_t>(PageId::kCount)> kPageHome{{
    {PageId::None, PageBuilder::None, std::nullopt},
    {PageId::Info, PageBuilder::FreeFactory, "InfoPage"},
    {PageId::Progress, PageBuilder::FreeFactory, "ProgressPage"},
    {PageId::Confirm, PageBuilder::MakePage, "ConfirmPage"},
    {PageId::Unlock, PageBuilder::None, std::nullopt},
    {PageId::SelectIni, PageBuilder::None, std::nullopt},
    {PageId::Browser, PageBuilder::MakePage, "BrowserPage"},
    {PageId::Recents, PageBuilder::None, std::nullopt},
    {PageId::Cheats, PageBuilder::MakePage, "CheatsPage"},
    {PageId::About, PageBuilder::None, std::nullopt},
    {PageId::System, PageBuilder::MakePage, "SystemPage"},
    {PageId::Common, PageBuilder::None, std::nullopt},
    {PageId::Misc, PageBuilder::None, std::nullopt},
    {PageId::VideoProc, PageBuilder::None, std::nullopt},
    {PageId::Uart, PageBuilder::None, std::nullopt},
    {PageId::UartMode, PageBuilder::None, std::nullopt},
    {PageId::UartBaud, PageBuilder::None, std::nullopt},
    {PageId::ScriptRun, PageBuilder::MakePage, "ScriptRunPage"},
    {PageId::FbTerminal, PageBuilder::None, std::nullopt},
    {PageId::CoreOptions, PageBuilder::MakePage, "CoreOptionsPage"},
    {PageId::ArcadeDip, PageBuilder::MakePage, "DipsPage"},
    {PageId::CoreSettings, PageBuilder::MakePage, "CoreSettingsPage"},
    {PageId::ConfigSlots, PageBuilder::MakePage, "ConfigSlotsPage"},
    {PageId::JoyMap, PageBuilder::MakePage, "JoyMapPage"},
    {PageId::KbdMap, PageBuilder::None, std::nullopt},
    {PageId::LightGunCal, PageBuilder::None, std::nullopt},
    {PageId::AdvancedMapList, PageBuilder::None, std::nullopt},
    {PageId::AdvancedMapEdit, PageBuilder::None, std::nullopt},
    {PageId::AdvancedMapButtons, PageBuilder::None, std::nullopt},
    {PageId::AdvancedMapCapture, PageBuilder::None, std::nullopt},
    {PageId::Mt32Pi, PageBuilder::None, std::nullopt},
    {PageId::HdMenu, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdCoreSettings, PageBuilder::MakePage, "CoreOptionsPage"},
    {PageId::HdCoreAbout, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdSettings, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdControllers, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdDisplay, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdAbout, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdMedia, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdSlotSheet, PageBuilder::MakePage, "HdListPage"},
    {PageId::HdRecentGames, PageBuilder::MakePage, "HdListPage"},
}};

consteval bool page_homes_are_indexed_by_id() {
    for (std::size_t i = 0; i < kPageHome.size(); ++i) {
        if (static_cast<std::size_t>(kPageHome[i].id) != i)
            return false;
    }
    return true;
}

static_assert(
    page_homes_are_indexed_by_id(),
    "kPageHome is indexed by PageId: a row out of place is a page with the wrong home"
);

consteval bool owner_present_iff_built() {
    for (const PageHome& row : kPageHome) {
        if (row.owner.has_value() != (row.builder != PageBuilder::None))
            return false;
    }
    return true;
}

static_assert(
    owner_present_iff_built(),
    "a page has an owner class exactly when something builds it"
);
static_assert(
    kPageHome.size() == static_cast<std::size_t>(PageId::kCount),
    "kPageHome has one row per PageId value; the closed sentinel has none"
);

}
