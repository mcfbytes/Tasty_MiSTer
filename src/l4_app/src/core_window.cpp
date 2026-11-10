// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_window.h"

#include <utility>

namespace mister::app {

CoreWindow::CoreWindow(hal::FpgaMemory window) noexcept : window_(std::move(window)) {}

Ex<std::size_t> CoreWindow::write(std::size_t off, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(CoreWindow);
    return window_.write_at(off, src);
}

Ex<std::size_t> CoreWindow::read(std::size_t off, std::span<std::byte> dst) const {
    TASTY_SEAT_BODY(CoreWindow);
    return window_.read_at(off, dst);
}

void CoreWindow::publish() noexcept {
    TASTY_SEAT_BODY(CoreWindow);
    window_.publish();
}

std::size_t CoreWindow::size() const noexcept {
    TASTY_SEAT_BODY(CoreWindow);
    return window_.region().len;
}

const hal::PhysRegion& CoreWindow::region() const noexcept {
    return window_.region();
}

}
