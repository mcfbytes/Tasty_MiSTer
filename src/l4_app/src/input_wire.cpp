// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input_wire.h"

#include "reactor/executive.h"

namespace mister::app {

bool InputWire::push_key(RawKeyEdge e) noexcept {
    if (keys_.push(e)) return true;
    note_key_drop(1u);
    return false;
}

bool InputWire::push_ui_key(RawKeyEdge e) noexcept {
    if (ui_keys_.push(e)) {
        bump(ui_keys_pub_, 1u);
        return true;
    }
    bump(ui_keys_dropped_, 1u);
    return false;
}

bool InputWire::kick_rt() noexcept {
    if (rt_exec_ == nullptr) return false;
    if (!arm_kick()) return false;
    rt_exec_->kick();
    return true;
}

Ex<void> InputWire::open_ctrl() noexcept {
    if (ctrl_.fd() >= 0) return {};
    return ctrl_.open_fd();
}

void InputWire::on_core_loaded(std::string_view core_name, bool front_end, const svc::JoyPlan& plan,
                               const svc::IAnalogReshape* analog_reshape) noexcept {

    buttons_.store(0u, std::memory_order_relaxed);
    bump(core_edges_, 1u);

    publish_core_name(core_name, front_end);

    publish_joy_plan(plan);
    publish_analog_reshape(analog_reshape);
    publish_edge_reset();
    request(kReqHotplug);
    wake();
}

void InputWire::on_core_detached() noexcept {

    publish_analog_reshape(nullptr);
    publish_edge_reset();

    buttons_.store(0u, std::memory_order_relaxed);
    bump(core_edges_, 1u);
}

}  // namespace mister::app
