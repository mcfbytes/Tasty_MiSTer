// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link/entry_line.h"
#include "proto/op_bitmap.h"
#include "proto/link/pad_shape.h"
#include "proto/link/sys_directory_image.h"
#include "proto/types.h"

namespace mister::proto::link {

struct PadBlockDecl {
    TASTY_SEAT_EXEMPT(const_shared);
    hal::LwOffset offset{};
    hal::LwExtent size{};
    OpBitmap ops{};
    PadShape shape{};
    EntryLine line{};
    hal::SysDirectory::EntryIndex index{};

    [[nodiscard]] constexpr bool keys() const noexcept {
        return ops.contains(UioOp::Mouse) && ops.contains(UioOp::Keyboard);
    }
};

[[nodiscard]] Ex<PadBlockDecl> find_pad_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

[[nodiscard]] bool pad_link_arm_absent(const SysDirectoryImage& dir, hal::LwExtent window) noexcept;

}
