// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/lane_block_decl.h"

#include <cstddef>
#include <utility>

#include "hal/uio_frame_lane.h"
#include "kinded_dir_entry.h"

namespace mister::proto {

namespace {

enum class LaneOp : std::size_t { Caps = 8, Fifo = 9 };

static_assert(
    ops_word(LaneOp::Caps) == 8 && ops_word(LaneOp::Fifo) == 9,
    "the lane entry's ops words are the ones lw's directory declares"
);

constexpr DirEntryFacts kFacts{
    DirEntryKind::Lane,
    DirMajor::V1,
    hal::LwExtent::of_layout<hal::UioFrameLane>()
};

}

[[nodiscard]] Ex<LaneBlockDecl> find_lane_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept {
    auto e = find_kinded<LaneOp>(dir, kFacts, window);
    if (!e)
        return std::unexpected(e.error());
    LaneBlockDecl out{.offset = e->offset(), .size = e->size()};
    out.ops = e->bitmap();
    out.caps = e->word(LaneOp::Caps).caps();
    out.fifo_depth = e->word(LaneOp::Fifo).high16();
    out.strobe_gap = e->word(LaneOp::Fifo).low16();
    out.line_decl = e->line();
    return out;
}

}
