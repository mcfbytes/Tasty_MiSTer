// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/osd_block.h"
#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "proto/link/entry_line.h"
#include "proto/link/osd_fifo_depth.h"
#include "proto/link/osd_shape.h"
#include "infra/prelude.h"
#include "proto/link/sys_directory_image.h"
#include "proto/types.h"

namespace mister::proto::link {

struct OsdBlockDecl {
    hal::LwOffset offset{};
    hal::LwExtent size{};
    OsdShape shape{};
    OsdFifoDepth fifo{};
    EntryLine line{};
    hal::SysDirectory::EntryIndex index{};
};

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
