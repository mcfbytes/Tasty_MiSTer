// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/io_main.h"

namespace mister::svc {

Ex<void> IoMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);
    return {};
}

bool IoMain::add_coworker(IIoCoworker* c) noexcept {
    if (c == nullptr || n_coworkers_ >= kMaxCoworkers) return false;
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (coworkers_[i] == c) return false;
    coworkers_[n_coworkers_++] = c;
    return true;
}

void IoMain::remove_coworker(IIoCoworker* c) noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i) {
        if (coworkers_[i] != c) continue;
        coworkers_[i] = coworkers_[--n_coworkers_];
        coworkers_[n_coworkers_] = nullptr;
        return;
    }
}

void IoMain::start() noexcept {
    TASTY_SEAT_BODY(IoMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "IoMain started unopened");
        return;
    }
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

bool IoMain::idle() const noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (!coworkers_[i]->idle()) return false;
    return true;
}

}  // namespace mister::svc
