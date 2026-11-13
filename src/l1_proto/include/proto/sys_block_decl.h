// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "proto/types.h"
#include "proto/declared_line.h"
#include "proto/sys_directory_image.h"
#include "os/types.h"

namespace mister::proto {

struct SysBlockDecl {
    hal::LwOffset offset{};
    hal::LwExtent size{};
    os::UioLine line{};
    hal::LwOffset cause{};
    CauseTag tag{};
    CauseBits causes{};
    DeclaredLine line_decl{};
};

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
