// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/block_device.h"

#include <sys/ioctl.h>

#include <cerrno>
#include <cstddef>

#if __has_include(<linux/fs.h>)
#include <linux/fs.h>
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif

namespace mister::os {

Ex<std::uint64_t> BlockDevice::size_bytes() const noexcept {
    unsigned long long blksize = 0;
    if (::ioctl(fd_, BLKGETSIZE64, &blksize) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return static_cast<std::uint64_t>(blksize);
}

}
