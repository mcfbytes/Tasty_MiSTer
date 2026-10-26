// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/sys_directory_image.h"

namespace mister::proto {

SysDirectoryImage SysDirectoryImage::read(
    const hal::RegisterWindow<hal::SysDirectory>& dir
) noexcept {
    std::array<std::uint32_t, kWords> w{};
    for (std::size_t i = 0; i < kWords; ++i) {
        w[i] = dir.read(static_cast<hal::SysDirectory::Reg>(i * 4));
    }
    return SysDirectoryImage{w};
}

}
