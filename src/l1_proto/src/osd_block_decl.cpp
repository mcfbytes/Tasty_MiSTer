// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/osd_block_decl.h"

#include <cstddef>
#include <utility>

#include "hal/osd_block.h"
#include "kinded_dir_entry.h"

namespace mister::proto {

namespace {

enum class OsdWord : std::size_t { Shape = 0 };

constexpr DirEntryFacts kFacts{
    DirEntryKind::Osd,
    DirMajor::V1,
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

    const OsdGeometry shape = e->word(OsdWord::Shape).osd_geometry();
    if (shape != OsdGeometry{hal::OsdBlock::kGeometry}) {
        return std::unexpected(shape.refused(Errc::bad_format, ERR_SITE()));
    }
    return OsdBlockDecl{.offset = e->offset(), .size = e->size(), .geometry = shape};
}

}
