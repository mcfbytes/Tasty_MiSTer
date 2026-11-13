// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/sys_block_decl.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/sys_block.h"
#include "kinded_dir_entry.h"
#include "proto/irq_binding.h"
#include "reactor/notifier.h"

namespace mister::proto {

namespace {

enum class SysWord : std::size_t {
    Ordinal = 0,
    Cause = 1,
    Tag = 2,
    Causes = 3,
};

constexpr DirEntryFacts kFacts{
    DirEntryKind::Sys,
    DirMajor::V1,
    hal::LwExtent::of_layout<hal::SysBlock>()
};

}

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept {
    auto e = find_kinded<SysWord>(dir, kFacts, window);
    if (!e)
        return std::unexpected(e.error());

    const IrqOrdinal ordinal = e->word(SysWord::Ordinal).irq_ordinal();
    if (ordinal < IrqOrdinal{kIrqPoolFirstLine} || ordinal > IrqOrdinal{kIrqOrdinalMax}) {
        return std::unexpected(ordinal.refused(Errc::slot_range, ERR_SITE()));
    }

    const CauseTag tag = e->word(SysWord::Tag).cause_tag();
    if (tag != CauseTag{reactor::cause_word::kSys}) {
        return std::unexpected(tag.refused(Errc::bad_format, ERR_SITE()));
    }
    const CauseBits causes = e->word(SysWord::Causes).cause_bits();
    if (!causes.holds(CauseBits{hal::SysBlock::kCauseStatus})) {
        return std::unexpected(causes.refused(Errc::not_found, ERR_SITE()));
    }
    return SysBlockDecl{
        .offset = e->offset(),
        .size = e->size(),
        .line = ordinal.line_after(IrqOrdinal{kIrqPoolFirstLine}),
        .cause = e->word(SysWord::Cause).lw_offset(),
        .tag = tag,
        .causes = causes,
        .line_decl = e->line()
    };
}

}
