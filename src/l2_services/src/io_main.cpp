// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/io_main.h"

#include <utility>

namespace mister::svc {

IoMain::IoMain(xthread::ParkFds fds, infra::OptRef<xthread::WakeFlag> asker, int doorbells) noexcept
    : wake_(fds.wake()),
      pause_(wake_, asker),
      stop_(std::move(fds).take_stop()),
      park_(wake_, stop_, doorbells) {}

Ex<void> IoMain::add_coworker(IIoCoworker& c) noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (coworkers_[i] == &c)
            return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    if (n_coworkers_ >= kMaxCoworkers)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), kMaxCoworkersDetail});
    coworkers_[n_coworkers_++] = &c;
    return {};
}

void IoMain::start() noexcept {
    TASTY_SEAT_BODY(IoMain);
    loop_();
}

void IoMain::serve() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        coworkers_[i]->serve();
}

void IoMain::on_pause() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        coworkers_[i]->on_pause();
}

void IoMain::on_resume() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        coworkers_[i]->on_resume();
}

void IoMain::settle() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        coworkers_[i]->on_stop();
}

bool IoMain::idle() const noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (!coworkers_[i]->idle())
            return false;
    return true;
}

}
