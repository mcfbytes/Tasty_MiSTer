// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/pad_block_decl.h"
#include "proto/link/pad_shape.h"

#include <cstddef>
#include <utility>

#include "hal/uio_reg_file.h"
#include "infra/prelude.h"
#include "kinded_dir_entry.h"
#include "proto/link/pad_levels.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

namespace {

enum class PadKindWord : std::size_t { Shape = 8, Flags = 9 };

static_assert(ops_word(PadKindWord::Shape) == 8, "the PAD entry's shape word is K8");

constexpr DirEntryFacts kFacts{
    DirEntryKind::Pad,
    kKnownMajor(DirEntryKind::Pad),
    hal::LwExtent::of_layout<hal::UioRegFile>()
};

}

template <>
inline constexpr bool kHasOpBitmap<PadKindWord> = true;

[[nodiscard]] Ex<PadBlockDecl> find_pad_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept {
    auto d = find_kinded<PadKindWord>(dir, kFacts, window);
    if (!d)
        return std::unexpected(d.error());

    for (const PadLevels::Reg& r : PadLevels::kRegs) {
        const UioOp op = r.opcode;
        if (!d->bitmap().contains(op)) {
            return std::unexpected(proto::word(op).refusal(Errc::bad_opcode, ERR_SITE()));
        }
    }

    const PadShape shape = d->word(PadKindWord::Shape).pad_shape();
    if (!(shape == PadShape::expected())) {
        return std::unexpected(shape.refusal(Errc::bad_format, ERR_SITE()));
    }
    return PadBlockDecl{
        .offset = d->offset(),
        .size = d->size(),
        .ops = d->bitmap(),
        .shape = shape,
        .line = d->line(),
        .index = d->index()
    };
}

bool pad_link_arm_absent(const SysDirectoryImage& dir, hal::LwExtent window) noexcept {
    auto d = find_kinded<PadKindWord>(dir, kFacts, window);
    if (!d)
        return false;
    return d->word(PadKindWord::Flags).pad_link_arm_absent();
}

static_assert(
    PadShape::expected() == PadShape{0x00100018u},
    "PAD 1.1: {spinner FIFO 16, keyed slots 24}, the K8 this revision declares"
);
static_assert(
    hal::PadBlock::kKeyedSlots == PadLevels::kCount,
    "the block's keyed slots are the level table's registers"
);

}
