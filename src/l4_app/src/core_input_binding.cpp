// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_input_binding.h"

namespace mister::app {
namespace {

constexpr cores::HostPolicy kDetachedFrontEnd{.front_end = true};

}

void CoreInputBinding::on_core_loaded(const CoreInputFacts& f) noexcept {

    core_edges_ = core_edges_.next();

    e_.facts.publish(f);
    last_ = f;
    e_.joy_edge_reset.request();
    e_.hotplug.request();
    e_.input_ctrl.kick();
}

void CoreInputBinding::on_core_detached() noexcept {

    last_.host = last_.policy().front_end
        ? infra::OptRef<const cores::HostPolicy>{kDetachedFrontEnd}
        : infra::OptRef<const cores::HostPolicy>{};
    e_.facts.publish(last_);
    e_.joy_edge_reset.request();
    core_edges_ = core_edges_.next();
}

}
