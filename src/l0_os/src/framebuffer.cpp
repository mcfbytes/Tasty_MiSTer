// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/framebuffer.h"

#include <linux/fb.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>

#include <cerrno>

namespace mister::os {

Framebuffer::Wait Framebuffer::wait_vsync() const noexcept {
    std::uint32_t zero = 0;
    if (::ioctl(fd_.get(), FBIO_WAITFORVSYNC, &zero) >= 0)
        return Wait::Edge;
    return errno == EINTR ? Wait::Interrupted : Wait::Failed;
}

std::optional<Framebuffer::Window> Framebuffer::window() const noexcept {
    fb_fix_screeninfo fix{};
    if (::ioctl(fd_.get(), FBIOGET_FSCREENINFO, &fix) < 0)
        return std::nullopt;
    return Window{.start = PhysAddr{fix.smem_start}, .len = MapLength{fix.smem_len}};
}

bool Framebuffer::flush() const noexcept {

    constexpr unsigned kRequestFlush = 0xA0;
    return ::ioctl(fd_.get(), _IO('F', kRequestFlush)) == 0;
}

}
