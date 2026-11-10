// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_build.h"

#include <string_view>
#include <utility>

#include "svc/input_service.h"

namespace mister::app {

InputBuild::InputBuild(const svc::Vfs& vfs, InputWire& wire, std::string_view input_dir)
    : wire_(wire),
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

    wire_.set_gate(InputWire::kGateGrabbed, svc::InputService::kBornGrabbed);
    wire_.set_gate(InputWire::kGateOsdVisible, false);
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
    if (!wire_.requested(InputWire::kReqHotplug))
        return false;

    auto loan = tx_.acquire();
    if (!loan) {
        wire_.note_rebuild_refused();
        return false;
    }

    wire_.clear_request(InputWire::kReqHotplug);

    if (auto e = en_.enumerate(); !e) {
        n_enum_fail_.add(1);
    }
    {

        const InputWire::CoreNameCell core = wire_.core_name();

        const auto reshape = wire_.analog_reshape();
        (void)en_.load_maps_for(core.view(), core.front_end, reshape, wire_.joy_plan());
    }
    hand_over_(std::move(loan));
    wire_.note_rebuild_done();
    wire_.wake();
    return true;
}

InputBuild::Counts InputBuild::counts() const noexcept {
    Counts c{};
    c.enumerate_failures = n_enum_fail_.get();
    c.devices = en_.stats().devices;
    c.rejected = en_.rejected_nodes();
    return c;
}

}
