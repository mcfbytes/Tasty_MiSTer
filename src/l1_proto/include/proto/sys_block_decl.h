// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "proto/block_line.h"
#include "proto/declared_line.h"
#include "proto/sys_directory_image.h"
#include "proto/types.h"

namespace mister::proto {

struct SysBlockDecl {
    hal::LwOffset offset{};
    hal::LwExtent size{};
    BlockLine line{};
    CauseTag tag{};
    CauseBits causes{};
};

struct SysBlockDeclLookup {
    SysBlockDecl block{};
    DeclaredLine declared{};
};

static_assert(
    line_source(DirEntryKind::Sys) == LineFrom::EntryWords,
    "a SYS entry's line is its own word 4, with word 5's CAUSE"
);

[[nodiscard]] Ex<SysBlockDeclLookup> find_sys_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
