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
    kCount
};

enum class BindPhase : std::uint8_t { Grant, Bind };

struct LinkRow {
    LinkKind kind;
    BindPhase phase;
    const char* token;
    const char* today;
};

inline constexpr auto kLinkRows = std::to_array<LinkRow>({
    {LinkKind::ImageSink, BindPhase::Grant,
     "AXI:", "the image sink over the session's own framing link"},
    {LinkKind::CoreWindow, BindPhase::Grant, "",
     "every declared window row, resolved through the board aperture; no row = no window"},
    {LinkKind::CensusRows, BindPhase::Bind, "",
     "the core's LinkDecoderDecl table, bound wholesale (item)"},
    {LinkKind::Doorbells, BindPhase::Bind,
     "IRQ:", "one notifier per declared binding; a refusal falls back to the row's poll"},
    {LinkKind::BlockSlots, BindPhase::Bind, "",
     "the save-slot binding table, mounted by the kind nibble (item)"},
});

consteval bool link_rows_cover_each_kind_once() {
    unsigned seen[static_cast<std::size_t>(LinkKind::kCount)] = {};
    for (const LinkRow& r : kLinkRows)
        ++seen[static_cast<std::size_t>(r.kind)];
    for (unsigned n : seen) {
        if (n != 1) return false;
    }
    return true;
}
static_assert(link_rows_cover_each_kind_once(),
              "kLinkRows declares each session link exactly once - a kind with no "
              "row is a link bound nowhere, a kind with two is bound twice");
static_assert(kLinkRows[0].kind == LinkKind::ImageSink && kLinkRows[0].phase == BindPhase::Grant,
              "grant_bulk() implements the first Grant-phase row; bind_session() "
              "walks the Bind-phase rows - this join is what keeps the table and "
              "the two walkers from drifting");
static_assert(kLinkRows[1].kind == LinkKind::CoreWindow && kLinkRows[1].phase == BindPhase::Grant,
              "grant_windows() implements the second Grant-phase row, and walks "
              "beside grant_bulk() because both products are constructor "
              "arguments to the same HostServices grant");

struct BindOutcome {
    std::uint32_t posted = 0;
    std::uint32_t decoders_refused = 0;
    std::uint32_t slots_refused = 0;
};

[[nodiscard]] inline BindOutcome bind_link_rows(std::span<const LinkRow> rows, LinkTxChannel& tx,
                                                [[maybe_unused]] std::uint32_t gen) noexcept {
    BindOutcome out{};
    for (const LinkRow& row : rows) {
        if (row.phase != BindPhase::Bind) continue;
        proto::LinkOp op{};
        switch (row.kind) {
            case LinkKind::BlockSlots:

                op = infra::make<proto::LinkOp>(proto::LinkOp::BindSlot{});
                break;
            case LinkKind::CensusRows:

                continue;
            case LinkKind::Doorbells:
            case LinkKind::ImageSink:
            case LinkKind::CoreWindow:
            case LinkKind::kCount:
                continue;
        }
        if (tx.push(op)) {
            ++out.posted;
        } else {
            ++out.slots_refused;
        }
    }
    return out;
}

}  // namespace mister::app
