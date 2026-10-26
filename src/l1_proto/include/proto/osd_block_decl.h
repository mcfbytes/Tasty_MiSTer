// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "infra/error.h"
#include "proto/sys_directory_image.h"

namespace mister::proto {

struct OsdBlockDecl {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint32_t geometry = 0;
};

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept;

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept;

}
