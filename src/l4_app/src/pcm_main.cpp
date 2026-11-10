// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pcm_main.h"

#include "app/pcm_command_dispatch.h"

namespace mister::app {

std::uint32_t PcmMain::drain_() noexcept {
    DrainSink sink{.feeder = feeder_};
    while (const auto c = cmds_.pop())
        infra::dispatch<app::PcmCommandRoutes>(*c, sink);
    return sink.discarded;
}

PcmMain::PcmMain(
    PcmRingFeeder& feeder,
    xthread::ParkFds fds,
    PcmRingFeeder::Commands& commands,
    std::optional<Mailbox> mailbox,
    infra::OptRef<xthread::WakeFlag> asker
) noexcept
    : feeder_(feeder),
      mailbox_(std::move(mailbox)),
      wake_(fds.wake()),
      stop_(std::move(fds).take_stop()),
      cmds_(commands),
      pause_(wake_, asker),
      park_(wake_, stop_) {}

void PcmMain::start() noexcept {
    TASTY_SEAT_BODY(PcmMain);
    loop_();
}

void PcmMain::serve() noexcept {
    if (stopping())
        return;
    serve_mailbox_();
    if (feeder_.take_park_ask()) {
        (void)drain_();
        feeder_.finish_park();
    }
    if (feeder_.parked())
        return;
    while (const auto c = cmds_.pop())
        infra::dispatch<app::PcmCommandRoutes>(*c, feeder_);
    feeder_.fill_pass();
}

void PcmMain::serve_mailbox_() noexcept {
    if (!mailbox_)
        return;
    mailbox_->host.serve();
    mailbox_->relay.serve_worker(mailbox_->host);
    mailbox_->host.refill_one();
}

bool PcmMain::mailbox_rest_() const noexcept {
    return !mailbox_ || (mailbox_->relay.worker_idle() && mailbox_->host.rest());
}

void PcmMain::on_pause() noexcept {
    paused_cmd_drops_ += drain_();
    feeder_.park_now();
    if (mailbox_) {
        mailbox_->relay.park_now();
        mailbox_->host.release();
    }
}

void PcmMain::settle() noexcept {
    (void)drain_();
    feeder_.release();
    if (mailbox_)
        mailbox_->host.release();
}

bool PcmMain::idle() const noexcept {
    if (stopping())
        return true;
    if (!mailbox_rest_())
        return false;
    if (!feeder_.rest())
        return false;
    return feeder_.parked() || inboxes_empty_();
}

}
