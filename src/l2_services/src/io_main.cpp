// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/io_main.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "infra/prelude.h"
#include "os/raw_monotonic_clock.h"

namespace mister::svc {

IoMain::IoMain(
    xthread::ParkFds fds,
    infra::OptRef<xthread::WakeFlag> asker,
    infra::OptRef<IoPassCell> pass
) noexcept
    : wake_(fds.wake()),
      pause_(wake_, asker),
      stop_(std::move(fds).take_stop()),
      pass_cell_(pass),
      park_(wake_, stop_) {}

IoMain::IoMain(
    xthread::ParkFds fds,
    infra::OptRef<xthread::WakeFlag> asker,
    const os::EpollSet& doorbells,
    infra::OptRef<IoPassCell> pass
) noexcept
    : wake_(fds.wake()),
      pause_(wake_, asker),
      stop_(std::move(fds).take_stop()),
      pass_cell_(pass),
      park_(wake_, stop_, doorbells.fd()) {}

Ex<void> IoMain::add_coworker(IIoCoworker& c, LaneClass cls) noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (lanes_[i].lane == &c)
            return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    if (n_coworkers_ >= kMaxCoworkers)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), kMaxCoworkersDetail});
    lanes_[n_coworkers_++] = Lane{&c, cls};
    return {};
}

void IoMain::start() noexcept {
    TASTY_SEAT_BODY(IoMain);
    loop_();
}

void IoMain::serve() noexcept {
    const os::RawMonotonicClock::time_point began = os::RawMonotonicClock::now();
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (lanes_[i].cls == LaneClass::Deadline)
            lanes_[i].lane->serve();
    bool deadline_busy = false;
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (lanes_[i].cls == LaneClass::Deadline && !lanes_[i].lane->idle())
            deadline_busy = true;
    if (deadline_busy) {
        bulk_deferred_.add();
        bulk_run_ += IoPassCount{1};
        pass_acc_.bulk_run_max = std::max(pass_acc_.bulk_run_max, bulk_run_);
    } else {
        bulk_run_ = IoPassCount{};
        for (std::uint8_t i = 0; i < n_coworkers_; ++i)
            if (lanes_[i].cls == LaneClass::Bulk)
                lanes_[i].lane->serve();
    }

    const std::chrono::nanoseconds took = os::RawMonotonicClock::now() - began;
    const bool longer = took > pass_acc_.max_ns;
    pass_acc_ = pass_acc_.with_pass(took);
    pass_acc_.bulk_deferred = IoPassCount{bulk_deferred_.get()};
    if (pass_cell_ && (longer || pass_acc_.passes.is_multiple_of(kIoPassPublishEvery)))
        pass_cell_->publish(pass_acc_);
}

void IoMain::on_pause() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        lanes_[i].lane->on_pause();
}

void IoMain::on_resume() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        lanes_[i].lane->on_resume();
}

void IoMain::settle() noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        lanes_[i].lane->on_stop();
}

bool IoMain::idle() const noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (!lanes_[i].lane->idle())
            return false;
    return true;
}

bool IoMain::quiescent() const noexcept {
    for (std::uint8_t i = 0; i < n_coworkers_; ++i)
        if (!lanes_[i].lane->quiescent())
            return false;
    return true;
}

}
