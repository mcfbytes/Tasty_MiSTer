// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pcm_main.h"

#include "app/pcm_command_dispatch.h"

namespace mister::app {

PcmMain::PcmMain(PcmRingFeeder& feeder) noexcept : feeder_(feeder) {}

Ex<void> PcmMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);

    feeder_.bind_commands(cmds_);
    return {};
}

void PcmMain::bind_mailbox(MailboxRelay& relay, CompanionHost& host) noexcept {
    relay_ = &relay;
    host_ = &host;
}

void PcmMain::start() noexcept {
    TASTY_SEAT_BODY(PcmMain);
    if (!park_) {
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "PcmMain started unopened");
        return;
    }
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
