// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "infra/error.h"
#include "proto/sys_directory_image.h"
#include "os/types.h"

namespace mister::proto {

struct SysBlockDecl {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    os::UioLine line{};
    std::uint32_t cause = 0;
    std::uint32_t tag = 0;
    std::uint32_t causes = 0;
    DeclaredLine line_decl{};
};

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept;

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept;

}
