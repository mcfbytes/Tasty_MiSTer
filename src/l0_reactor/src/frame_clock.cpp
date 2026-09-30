// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/frame_clock.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>

namespace mister::reactor {

namespace {

constexpr long kBackOffNs = 4'000'000;

void back_off() {
    timespec ts{};
    ts.tv_sec = 0;
    ts.tv_nsec = kBackOffNs;
    int rc = 0;
    do {
        rc = ::clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts);
    } while (rc == EINTR);
}

}  // namespace

Ex<std::unique_ptr<FrameClock>> FrameClock::open(const char* fbdev_path) {
    if (fbdev_path == nullptr) return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    const int fb = ::open(fbdev_path, O_RDWR | O_CLOEXEC);
    if (fb < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    std::unique_ptr<FrameClock> fc(new FrameClock());
    fc->fb_fd_.reset(fb);
    if (auto w = fc->frame_wake_.open_fd(); !w) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), w.error().detail});
    }
    return fc;
}

void FrameClock::wait_vsync() noexcept {
    TASTY_SEAT_BODY(FrameClock);
    std::uint32_t zero = 0;
    if (::ioctl(fb_fd_.get(), FBIO_WAITFORVSYNC, &zero) < 0) {
        if (errno == EINTR) return;
        back_off();
        return;
    }
    const std::uint32_t n_seq = seq_.fetch_add(1, std::memory_order_release) + 1u;

    cell_.publish(FrameRecord{n_seq});
    frame_wake_.kick();
}

}  // namespace mister::reactor
