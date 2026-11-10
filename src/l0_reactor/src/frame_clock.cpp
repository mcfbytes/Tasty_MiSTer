// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/frame_clock.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>
#include <utility>

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

}

Ex<std::unique_ptr<FrameClock>> FrameClock::open(const char* fbdev_path) {
    if (fbdev_path == nullptr)
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    UniqueFd fb{::open(fbdev_path, O_RDWR | O_CLOEXEC)};
    if (!fb.valid()) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    std::unique_ptr<FrameClock> fc(new FrameClock());
    fc->fb_ = os::Framebuffer::adopt(std::move(fb));
    if (auto w = fc->frame_wake_.open_fd(); !w) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), w.error().detail});
    }
    return fc;
}

void FrameClock::wait_vsync() noexcept {
    TASTY_SEAT_BODY(FrameClock);
    switch (fb_.wait_vsync()) {
        case os::Framebuffer::Wait::Edge:
            break;
        case os::Framebuffer::Wait::Interrupted:
            return;
        case os::Framebuffer::Wait::Failed:
            back_off();
            return;
    }
    seq_ = seq_.next();

    cell_.publish(FrameRecord{seq_});
    frame_wake_.kick();
}

}
