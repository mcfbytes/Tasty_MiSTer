// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/link_tx_channel.h"
#include "proto/link_op.h"

namespace mister::app {

enum class LinkKind : std::uint8_t {
    ImageSink,
    CoreWindow,
    CensusRows,
    Doorbells,
    BlockSlots,
    Pads,
    Sectors,
    kCount
};

enum class BindPhase : std::uint8_t { Grant, Bind };

enum class MainAct : std::uint8_t {
    GrantBulk,
    GrantWindows,
    GrantPads,
    GrantSector,
    MintBindSlot,
    None,
    kCount
};
enum class RtAct : std::uint8_t { BindCensus, BindDoorbells, None, kCount };

struct LinkRow {
    LinkKind kind;
    BindPhase phase;
    MainAct main;
    RtAct rt;
    const char* token;
    const char* today;
};

inline constexpr auto kLinkRows = std::to_array<LinkRow>({
    {LinkKind::ImageSink,
     BindPhase::Grant,
     MainAct::GrantBulk,
     RtAct::None,
     "AXI:",
     "the image sink over the session's own framing link"},
    {LinkKind::CoreWindow,
     BindPhase::Grant,
     MainAct::GrantWindows,
     RtAct::None,
     "",
     "every declared window row, resolved through the board aperture; no row = no window"},
    {LinkKind::CensusRows,
     BindPhase::Bind,
     MainAct::None,
     RtAct::BindCensus,
     "",
     "the core's LinkDecoderDecl table, bound wholesale (item)"},
    {LinkKind::Doorbells,
     BindPhase::Bind,
     MainAct::None,
     RtAct::BindDoorbells,
     "IRQ:",
     "one notifier per declared binding; a refusal falls back to the row's poll"},
    {LinkKind::BlockSlots,
     BindPhase::Bind,
     MainAct::MintBindSlot,
     RtAct::None,
     "",
     "the save-slot binding table, mounted by the kind nibble (item)"},
    {LinkKind::Pads,
     BindPhase::Grant,
     MainAct::GrantPads,
     RtAct::None,
     "AXI:",
     "MAIN grants a declared LW window's SYS1 PAD block; T-RT stores pads there, else the link"},
    {LinkKind::Sectors,
     BindPhase::Grant,
     MainAct::GrantSector,
     RtAct::None,
     "AXI:",
     "MAIN grants a SYS1 SECTOR block and its BLK row to T-IO; else the slot stays on the link"},
});

[[nodiscard]] consteval bool grants_before_core(MainAct act) noexcept {
    return act == MainAct::GrantBulk
        || act == MainAct::GrantWindows
        || act == MainAct::GrantPads
        || act == MainAct::GrantSector;
}

consteval bool link_rows_predict_both_seats() {
    unsigned kinds[static_cast<std::size_t>(LinkKind::kCount)] = {};
    unsigned mains[static_cast<std::size_t>(MainAct::kCount)] = {};
    unsigned rts[static_cast<std::size_t>(RtAct::kCount)] = {};
    for (const LinkRow& r : kLinkRows) {
        if (r.kind >= LinkKind::kCount || r.main >= MainAct::kCount || r.rt >= RtAct::kCount)
            return false;
        ++kinds[static_cast<std::size_t>(r.kind)];
        ++mains[static_cast<std::size_t>(r.main)];
        ++rts[static_cast<std::size_t>(r.rt)];
        if (r.phase == BindPhase::Grant) {
            if (r.rt != RtAct::None || !grants_before_core(r.main))
                return false;
            continue;
        }
        if (grants_before_core(r.main))
            return false;
        if (r.main == MainAct::None && r.rt == RtAct::None)
            return false;
    }
    for (const unsigned n : kinds) {
        if (n != 1)
            return false;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(MainAct::kCount); ++i) {
        if (i == static_cast<std::size_t>(MainAct::None))
            continue;
        if (mains[i] != 1)
            return false;
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(RtAct::kCount); ++i) {
        if (i == static_cast<std::size_t>(RtAct::None))
            continue;
        if (rts[i] != 1)
            return false;
    }
    return true;
}

static_assert(
    link_rows_predict_both_seats(),
    "kLinkRows declares each session link once and names BOTH seats' work: a Grant "
    "row is MAIN's alone, a Bind row names at least one seat, and no act is claimed "
    "twice - a kind added without a row, or a row that predicts neither walker, does "
    "not compile"
);

struct BindOutcome {
    std::uint32_t decoders_refused = 0;
    std::uint32_t slots_refused = 0;
};

[[nodiscard]] inline BindOutcome bind_link_rows(
    std::span<const LinkRow> rows,
    LinkTxChannel<SeatTag::Main>& tx,
    [[maybe_unused]] proto::BindGeneration gen
) noexcept {
    BindOutcome out{};
    for (const LinkRow& row : rows) {
        if (row.phase != BindPhase::Bind)
            continue;
        proto::LinkOp op{};
        switch (row.main) {
            case MainAct::MintBindSlot:

                op = infra::make<proto::LinkOp>(proto::LinkOp::BindSlot{});
                break;
            case MainAct::GrantBulk:
            case MainAct::GrantWindows:
            case MainAct::GrantPads:
            case MainAct::GrantSector:
            case MainAct::None:

                continue;
            case MainAct::kCount:
                continue;
        }
        if (!tx.push(op))
            ++out.slots_refused;
    }
    return out;
}

}
