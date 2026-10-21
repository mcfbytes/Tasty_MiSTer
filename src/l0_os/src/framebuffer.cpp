// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/framebuffer.h"

#include <linux/fb.h>
#include <sys/ioctl.h>

#include <cerrno>

namespace mister::os {

Framebuffer::Wait Framebuffer::wait_vsync() const noexcept {
    std::uint32_t zero = 0;
    if (::ioctl(fd_.get(), FBIO_WAITFORVSYNC, &zero) >= 0)
        return Wait::Edge;
    return errno == EINTR ? Wait::Interrupted : Wait::Failed;
}

}
