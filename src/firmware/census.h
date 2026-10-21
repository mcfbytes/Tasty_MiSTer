// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "reactor/executive.h"

namespace mister::fw {

struct DocCensus {
    std::string_view core;
    std::span<const reactor::LinkDecoderDecl> rows;
};

inline constexpr auto kMegaCdDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"megacd.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     0,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Shared},
    {"megacd.service_tick",
     reactor::Cause::Blk,
     nullptr,
     10,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kPceCdDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"pcecd.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     0,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Shared},
    {"pcecd.service_tick",
     reactor::Cause::Tick,
     nullptr,
     13,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kPsxDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"psx.sector_kick",
     reactor::Cause::Tick,
     nullptr,
     0,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Shared},
});

inline constexpr auto kAtariStDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"st.acsi_poll",
     reactor::Cause::Tick,
     nullptr,
     1,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kThreeDoDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"3do.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     3,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
    {"3do.service_tick",
     reactor::Cause::Tick,
     nullptr,
     3,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kSnesDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"snes.mailbox",
     reactor::Cause::Tick,
     nullptr,
     0,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Shared},
});

inline constexpr auto kSgbDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"sgb.mailbox",
     reactor::Cause::Tick,
     nullptr,
     0,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Shared},
});

inline constexpr auto kMegaDriveDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"megadrive.mdp_tick",
     reactor::Cause::Tick,
     nullptr,
     5,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kN64DocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"n64.cheat_tick",
     reactor::Cause::Tick,
     nullptr,
     16,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kSaturnDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"saturn.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     1,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
    {"saturn.service_tick",
     reactor::Cause::Tick,
     nullptr,
     1,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kNeoGeoDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"neogeo.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     0,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Shared},
    {"neogeo.service_tick",
     reactor::Cause::Tick,
     nullptr,
     1,
     reactor::DeadlineClass::A,
     reactor::OsdBudget::Pinned},
});

inline constexpr auto kDocCensus = std::to_array<DocCensus>({
    {"MEGACD", kMegaCdDocRows},
    {"TGFX16", kPceCdDocRows},
    {"PSX", kPsxDocRows},
    {"SNES", kSnesDocRows},
    {"SGB", kSgbDocRows},
    {"AtariST", kAtariStDocRows},
    {"3DO", kThreeDoDocRows},
    {"MegaDrive", kMegaDriveDocRows},
    {"N64", kN64DocRows},
    {"Saturn", kSaturnDocRows},
    {"NEOGEO", kNeoGeoDocRows},
});

[[nodiscard]] Ex<void> verify_census();

}
