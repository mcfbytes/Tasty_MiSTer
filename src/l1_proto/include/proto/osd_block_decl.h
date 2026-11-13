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

namespace mister::proto {

struct OsdBlockDecl {
    hal::LwOffset offset{};
    hal::LwExtent size{};
    OsdGeometry geometry{};
};

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept;

}
