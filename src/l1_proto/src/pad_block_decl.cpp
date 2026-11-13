// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/pad_block_decl.h"

#include <utility>

#include "hal/uio_reg_file.h"
#include "kinded_dir_entry.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

namespace {

constexpr DirEntryFacts kFacts{
    DirEntryKind::Pad,
    DirMajor::V1,
    hal::LwExtent::of_layout<hal::UioRegFile>()
};

}

[[nodiscard]] Ex<PadBlockDecl> find_pad_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept {
    auto d = find_kinded<NoWords>(dir, kFacts, window);
    if (!d)
        return std::unexpected(d.error());
    PadBlockDecl
        out{.offset = d->offset(), .size = d->size(), .ops = d->bitmap(), .line = d->line()};
    for (const UioOp op : {UioOp::Joystick0, UioOp::Joystick1}) {
        if (!out.ops.honours(op)) {
            return std::unexpected(proto::word(op).refused(Errc::bad_opcode, ERR_SITE()));
        }
    }
    return out;
}

}
