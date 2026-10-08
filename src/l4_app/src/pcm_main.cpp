// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pcm_main.h"

#include "app/pcm_command_dispatch.h"

namespace mister::app {

PcmMain::PcmMain(PcmRingFeeder& feeder, xthread::ParkFds fds, PcmRingFeeder::Commands& commands,
                 std::optional<Mailbox> mailbox, infra::OptRef<xthread::WakeFlag> asker) noexcept
    : feeder_(feeder), relay_(mailbox ? &mailbox->relay : nullptr),
      host_(mailbox ? &mailbox->host : nullptr), wake_(fds.wake()),
      stop_(std::move(fds).take_stop()), cmds_(commands), pause_(wake_, asker ? &*asker : nullptr),
      park_(wake_, stop_) {}

void PcmMain::start() noexcept {
    TASTY_SEAT_BODY(PcmMain);
    loop_();
}

void PcmMain::serve() noexcept {
    if (stopping()) return;
    serve_mailbox_();
    if (feeder_.take_park_ask()) {
        while (cmds_.pop().has_value()) {
        }
        feeder_.finish_park();
    }
    if (feeder_.parked()) return;
    while (const auto c = cmds_.pop())
        infra::dispatch<app::PcmCommandRoutes>(*c, feeder_);
    feeder_.fill_pass();
}

void PcmMain::serve_mailbox_() noexcept {
    if (relay_ == nullptr) return;
    host_->serve();
    relay_->serve_worker(*host_);
    host_->refill_one();
}

bool PcmMain::mailbox_rest_() const noexcept {
    return relay_ == nullptr || (relay_->worker_idle() && host_->rest());
}

void PcmMain::on_pause() noexcept {
    while (cmds_.pop().has_value())
        ++paused_cmd_drops_;
    feeder_.park_now();
    if (relay_ != nullptr) {
        relay_->park_now();
        host_->release();
    }
}

void PcmMain::settle() noexcept {
    feeder_.release();
    if (host_ != nullptr) host_->release();
}

bool PcmMain::idle() const noexcept {
    if (stopping()) return true;
    if (!mailbox_rest_()) return false;
    if (!feeder_.rest()) return false;
    return feeder_.parked() || inboxes_empty_();
}

}  // namespace mister::app
