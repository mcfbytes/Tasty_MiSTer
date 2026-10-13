// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/block_device.h"

#include <sys/ioctl.h>

#include <cstddef>

#if __has_include(<linux/fs.h>)
#include <linux/fs.h>
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

namespace mister::os {

std::optional<std::uint64_t> BlockDevice::size_bytes() const noexcept {
    unsigned long long blksize = 0;
    if (::ioctl(fd_, BLKGETSIZE64, &blksize) < 0) return std::nullopt;
    return static_cast<std::uint64_t>(blksize);
}

}  // namespace mister::os
