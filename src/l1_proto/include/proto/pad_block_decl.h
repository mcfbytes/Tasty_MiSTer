// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/types.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/declared_line.h"
#include "proto/op_bitmap.h"
#include "proto/sys_directory_image.h"

namespace mister::proto {

struct PadBlockDecl {
    TASTY_SEAT_EXEMPT(const_shared);
    hal::LwOffset offset{};
    hal::LwExtent size{};
    OpBitmap ops{};
    DeclaredLine line{};
};

[[nodiscard]] Ex<PadBlockDecl> find_pad_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
