// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "cores/core_profile.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "reactor/executive.h"

namespace mister::fw {

struct DocCensus {
    cores::CoreKind kind;
    std::span<const reactor::LinkDecoderDecl> rows;
};

inline constexpr auto kMegaCdDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"megacd.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
    {"megacd.service_tick",
     reactor::Cause::Blk,
     nullptr,
     std::chrono::milliseconds{10},
     reactor::RoundClaim::Always},
});

inline constexpr auto kPceCdDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"pcecd.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
    {"pcecd.service_tick",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{13},
     reactor::RoundClaim::Always},
});

inline constexpr auto kPsxDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"psx.sector_kick",
     reactor::Cause::Tick,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
});

inline constexpr auto kAtariStDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"st.acsi_poll",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{1},
     reactor::RoundClaim::Always},
});

inline constexpr auto kThreeDoDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"3do.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     std::chrono::milliseconds{3},
     reactor::RoundClaim::Always},
    {"3do.service_tick",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{3},
     reactor::RoundClaim::Always},
});

inline constexpr auto kSnesDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"snes.mailbox",
     reactor::Cause::Tick,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
});

inline constexpr auto kSgbDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"sgb.mailbox",
     reactor::Cause::Tick,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
});

inline constexpr auto kMegaDriveDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"megadrive.mdp_tick",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{5},
     reactor::RoundClaim::Always},
});

inline constexpr auto kN64DocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"n64.cheat_tick",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{16},
     reactor::RoundClaim::Always},
});

inline constexpr auto kSaturnDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"saturn.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     std::chrono::milliseconds{1},
     reactor::RoundClaim::Always},
    {"saturn.service_tick",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{1},
     reactor::RoundClaim::Always},
});

inline constexpr auto kNeoGeoDocRows = std::to_array<reactor::LinkDecoderDecl>({
    {"neogeo.cmd_edge",
     reactor::Cause::Ring,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
    {"neogeo.service_tick",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{1},
     reactor::RoundClaim::Always},
});

inline constexpr auto kDocCensus = std::to_array<DocCensus>({
    {cores::CoreKind::MegaCd, kMegaCdDocRows},
    {cores::CoreKind::PceCd, kPceCdDocRows},
    {cores::CoreKind::Psx, kPsxDocRows},
    {cores::CoreKind::Snes, kSnesDocRows},
    {cores::CoreKind::Sgb, kSgbDocRows},
    {cores::CoreKind::AtariSt, kAtariStDocRows},
    {cores::CoreKind::ThreeDo, kThreeDoDocRows},
    {cores::CoreKind::MegaDrive, kMegaDriveDocRows},
    {cores::CoreKind::N64, kN64DocRows},
    {cores::CoreKind::Saturn, kSaturnDocRows},
    {cores::CoreKind::NeoGeo, kNeoGeoDocRows},
});

namespace detail {

consteval bool doc_census_keys_are_distinct() {
    for (std::size_t i = 0; i < kDocCensus.size(); ++i) {
        for (std::size_t j = i + 1; j < kDocCensus.size(); ++j) {
            if (kDocCensus[i].kind == kDocCensus[j].kind)
                return false;
        }
    }
    return true;
}

consteval bool doc_census_keys_ship_rows() {
    for (const DocCensus& d : kDocCensus) {
        if (d.kind == cores::CoreKind::Generic || d.kind == cores::CoreKind::Count)
            return false;
    }
    return true;
}

}

static_assert(
    detail::doc_census_keys_are_distinct(),
    "one document table per CoreKind: a repeated key makes the census join ambiguous"
);
static_assert(
    detail::doc_census_keys_ship_rows(),
    "Generic ships no census rows and Count is not a kind: neither may be a doc key"
);

[[nodiscard]] Ex<void> verify_census();

}
