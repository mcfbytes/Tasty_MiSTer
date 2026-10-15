// SPDX-License-Identifier: GPL-3.0-or-later
#include "process_main.h"

#include "supervisor_verdict.h"
#include "thread_assembly.h"
#include "app/link_event_dispatch.h"
#include "app/link_rx_channel.h"
#include "app/session_owner.h"
#include "app/ui_request_dispatch.h"
#include "infra/message_sum.h"

namespace mister::fw {

ProcessMain::ProcessMain(app::SessionOwner& owner, const ThreadAssembly& assembly,
                         xthread::ParkFds fds) noexcept
    : owner_(owner), assembly_(assembly), stop_(fds.stop_wire()), park_(fds.wake(), stop_) {}

void ProcessMain::start() noexcept {
    TASTY_SEAT_BODY(ProcessMain);
    owner_.on_seats_live();
    loop_();
}

void ProcessMain::pass(app::SessionOwner& owner) noexcept {
    seat_assert<SeatTag::Main>(ERR_SITE(), "ProcessMain::pass off MAIN");
    while (const auto e = owner.rx().pop()) {
        infra::dispatch<app::LinkEventRoutes<app::EventSink::Main>>(*e, owner);
    }

    while (const auto req = owner.ui_requests().pop()) {
        infra::dispatch<app::UiRequestRoutes>(*req, owner);
    }
    (void)owner.tick();
}

void ProcessMain::serve() noexcept { pass(owner_); }

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
