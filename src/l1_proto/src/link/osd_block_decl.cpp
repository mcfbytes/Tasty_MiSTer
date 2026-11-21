// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/osd_block_decl.h"
#include "proto/link/osd_fifo_depth.h"
#include "proto/link/osd_shape.h"

#include <cstddef>
#include <utility>

#include "hal/osd_block.h"
#include "infra/prelude.h"
#include "kinded_dir_entry.h"

namespace mister::proto::link {

namespace {

enum class OsdWord : std::size_t { Shape = 0, Fifo = 1 };

constexpr DirEntryFacts kFacts{
    DirEntryKind::Osd,
    kKnownMajor(DirEntryKind::Osd),
    hal::LwExtent::of_layout<hal::OsdBlock>()
};

}

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept {
    auto e = find_kinded<OsdWord>(dir, kFacts, window);
    if (!e)
        return std::unexpected(e.error());

    const OsdShape shape = e->word(OsdWord::Shape).osd_shape();
    if (!shape.admits())
        return std::unexpected(shape.refusal(Errc::bad_format, ERR_SITE()));

    const OsdFifoDepth fifo = e->word(OsdWord::Fifo).osd_fifo();
    if (!(fifo == OsdFifoDepth::declared())) {
        return std::unexpected(fifo.refusal(Errc::bad_format, ERR_SITE()));
    }
    return OsdBlockDecl{
        .offset = e->offset(),
        .size = e->size(),
        .shape = shape,
        .fifo = fifo,
        .line = e->line(),
        .index = e->index()
    };
}

static_assert(!CarriesOpBitmap<OsdWord>, "an OSDBUF entry declares no ops bitmap");

}
