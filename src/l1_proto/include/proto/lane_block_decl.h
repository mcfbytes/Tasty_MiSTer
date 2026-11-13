// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "proto/types.h"
#include "proto/declared_line.h"
#include "proto/op_bitmap.h"
#include "proto/sys_directory_image.h"

namespace mister::proto {

struct LaneBlockDecl {
    hal::LwOffset offset{};
    hal::LwExtent size{};
    OpBitmap ops{};
    LaneCaps caps{};
    std::uint16_t fifo_depth = 0;
    std::uint16_t strobe_gap = 0;
    DeclaredLine line_decl{};
};

[[nodiscard]] Ex<LaneBlockDecl> find_lane_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
