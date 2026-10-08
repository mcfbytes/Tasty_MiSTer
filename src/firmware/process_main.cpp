// SPDX-License-Identifier: GPL-3.0-or-later
#include "process_main.h"

#include "report.h"
#include "supervisor_verdict.h"
#include "thread_assembly.h"
#include "app/link_event_dispatch.h"
#include "app/link_rx_channel.h"
#include "app/session_owner.h"
#include "app/ui_request_dispatch.h"
#include "infra/message_sum.h"

namespace mister::fw {

ProcessMain::ProcessMain(app::SessionOwner& owner, const ThreadAssembly& assembly,
                         xthread::WakeFlag& wake) noexcept
    : owner_(owner), assembly_(assembly), wake_(wake) {}

Ex<void> ProcessMain::open() noexcept {
    if (wake_.fd() < 0) {
        if (auto r = wake_.open_fd(); !r) return r;
    }
    if (stop_.fd() < 0) {
        if (auto r = stop_.open_fd(); !r) return r;
    }
    if (!park_) park_.emplace(wake_, stop_);
    return {};
}

void ProcessMain::start() noexcept {
    if (!park_) {
        report("ProcessMain::start", Error{Errc::negotiation, ERR_SITE(), 0});
        if constexpr (kSeatChecksEnabled)
            fatal(Error{Errc::negotiation, ERR_SITE(), 0}, "ProcessMain started unopened");
        return;
    }
    owner_.on_seats_live();
    loop_();
}

void ProcessMain::serve() noexcept {
    while (const auto e = owner_.rx().pop()) {
        infra::dispatch<app::LinkEventRoutes<app::EventSink::Main>>(*e, owner_);
    }

    while (const auto req = owner_.ui_requests().pop()) {
        infra::dispatch<app::UiRequestRoutes>(*req, owner_);
    }
    (void)owner_.tick();
}

bool ProcessMain::idle() const noexcept {
    if (stopping()) return true;
    if (owner_.rx().size() != 0 || owner_.ui_requests().size() != 0) return false;

    if (owner_.reboot_owed()) return true;
    return owner_.park_acks_empty() && !owner_.program_owed() && !owner_.rung_pending();
}

int ProcessMain::park_ms() const noexcept { return owner_.wake_hint_ms(kSupervisorWakeMs); }

bool ProcessMain::stopping() const noexcept {
    return supervisor_verdict(stop_.ever_requested(), assembly_.rt_exited()) ==
           SupervisorVerdict::Exit;
}

}  // namespace mister::fw
