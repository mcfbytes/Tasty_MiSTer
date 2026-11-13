// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_build.h"

#include <string_view>
#include <utility>

namespace mister::app {

InputBuild::InputBuild(const svc::Vfs& vfs, Ends ends, std::string_view input_dir)
    : ends_(ends),
      en_(svc::InputEnumerator::create(vfs, input_dir)) {

    if (auto e = en_.enumerate(); !e) {
        n_enum_fail_.add(1);
    }

    (void)en_.load_maps_for(std::string_view{}, false);

    {
        const SeatScope stands_in_for_diag{kSeat};
        if (auto loan = tx_.acquire())
            hand_over_(std::move(loan));
    }
}

void InputBuild::set_cfg_deadzone_rules(std::span<const svc::DeadzoneRule> rows) {
    en_.set_cfg_deadzone_rules(rows);
}

void InputBuild::hand_over_(DeviceTableLoans::Loan loan) {
    svc::DeviceTable t = en_.take_table();
    std::swap(*loan, t);
    tx_.send(std::move(loan));
}

bool InputBuild::rebind_round() {

    TASTY_SEAT_BODY(InputBuild);

    while (tx_.reap()) {
    }
    if (!hotplug_seen_.peek(ends_.hotplug))
        return false;

    auto loan = tx_.acquire();
    if (!loan) {
        rebuilds_refused_.add();
        return false;
    }

    (void)hotplug_seen_.take(ends_.hotplug);

    if (auto e = en_.enumerate(); !e) {
        n_enum_fail_.add(1);
    }
    {

        const CoreInputFacts facts = ends_.facts.sample().value;
        (void)en_.load_maps_for(facts.core.view(), facts.front_end, facts.reshape, facts.plan);
    }
    hand_over_(std::move(loan));
    rebuilds_done_.add();
    ends_.input_ctrl.kick();
    return true;
}

InputBuild::Counts InputBuild::counts() const noexcept {
    Counts c{};
    c.enumerate_failures = n_enum_fail_.get();
    c.devices = en_.stats().devices;
    c.rejected = en_.rejected_nodes();
    c.rebuilds_done = rebuilds_done_;
    c.rebuilds_refused = rebuilds_refused_;
    return c;
}

}
