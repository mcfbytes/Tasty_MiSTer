// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/sys_block_decl.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/sys_block.h"
#include "infra/prelude.h"
#include "kinded_dir_entry.h"
#include "proto/irq_binding.h"

namespace mister::proto::link {

namespace {

enum class SysWord : std::size_t { StatusWords = 0 };

constexpr DirEntryFacts kFacts{
    DirEntryKind::Sys,
    kKnownMajor(DirEntryKind::Sys),
    hal::LwExtent::of_layout<hal::SysBlock>()
};

}

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const SysDirectoryImage& dir,
    const AxiLwDecl& token
) noexcept {
    auto e = find_kinded<SysWord>(dir, kFacts, token.size);
    if (!e)
        return std::unexpected(e.error());

    const IrqOrdinal ordinal = e->line().ordinal;
    if (!e->line().declared()
        || ordinal < IrqOrdinal{kIrqPoolFirstLine}
        || ordinal > IrqOrdinal{kIrqOrdinalMax}) {
        return std::unexpected(ordinal.refusal(Errc::slot_range, ERR_SITE()));
    }
    const CauseBits causes = e->cause_bits();
    if (!causes.holds(CauseBits{hal::SysBlock::kCauseStatus})) {
        return std::unexpected(causes.refusal(Errc::not_found, ERR_SITE()));
    }

    const DirWord status = e->word(SysWord::StatusWords);
    if (status.low16() != hal::SysBlock::kSnapshotWords) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), status.low16()});
    }
    return SysBlockDecl{
        .offset = e->offset(),
        .size = e->size(),
        .line =
            BlockLine{
                .line = ordinal.line_after(IrqOrdinal{kIrqPoolFirstLine}),
                .cause = token.offset.plus(e->cause())
            },
        .causes = causes,
        .index = e->index()
    };
}

static_assert(!CarriesOpBitmap<SysWord>, "an EVT entry declares no ops bitmap");

}
