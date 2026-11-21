// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "proto/axi_lw_decl.h"
#include "infra/prelude.h"
#include "proto/block_line.h"
#include "proto/link/sys_directory_image.h"
#include "proto/types.h"

namespace mister::proto::link {

struct SysBlockDecl {
    hal::LwOffset offset{};
    hal::LwExtent size{};
    BlockLine line{};
    CauseBits causes{};
    hal::SysDirectory::EntryIndex index{};
};

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const SysDirectoryImage& dir,
    const AxiLwDecl& token
) noexcept;

}
