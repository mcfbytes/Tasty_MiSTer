// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_pipeline.h"

#include <type_traits>
#include <utility>

namespace mister::app {

static_assert(
    std::is_constructible_v<
        InputPipeline,
        InputDecode::Fds,
        InputWire&,
        InputBuild&,
        InputEmit&,
        svc::InputService,
        os::IClock&,
        InputDecode::Wiring>,
    "the intended constructor must still exist"
);
static_assert(
    !std::is_default_constructible_v<InputPipeline>,
    "an unopened pipeline must have no representation"
);

InputPipeline::InputPipeline(
    InputDecode::Fds fds,
    InputWire& wire,
    InputBuild& build,
    InputEmit& emit,
    svc::InputService service,
    os::IClock& clock,
    InputDecode::Wiring decode_wiring
) noexcept
    : wire_(wire),
      build_(build),
      emit_(emit),
      service_(std::move(service)),
      decode_(std::move(fds), wire, service_, build.tables(), clock, decode_wiring),
      sample_{
          infra::OptRef<const InputDecode::CountsCell>{decode_.counts_cell()},
          infra::OptRef<const InputEmit::CountsCell>{emit_.counts_cell()},
          infra::OptRef<const InputWire>{wire_}
      } {}

void InputPipeline::latch_cfg(std::span<const svc::DeadzoneRule> deadzones, bool rumble) noexcept {
    service_.apply_deadzone_rules(deadzones);
    service_.set_cfg_rumble(rumble);
}

InputStats collect_input_stats(const InputSample& s, const InputBuild::Counts& b) noexcept {
    InputStats out{};
    if (s.decode) {
        const InputDecode::Counts d = s.decode->sample().value;
        out.rounds = d.rounds;
        out.events = d.events;
        out.keys_published = d.keys_published;
        out.keys_emitted = d.keys_emitted;
        out.mouse_packets = d.mouse_packets;
        out.kicks = d.kicks;
        out.fd_evictions = d.fd_evictions;
        out.registered = d.registered;
        out.joy_seen = d.joy_seen;
        out.joy_players = d.joy_players;
        out.captures = d.captures;
        out.service = d.service;
    }
    out.enumerate_failures = b.enumerate_failures;
    out.devices = b.devices;
    out.rejected = b.rejected;
    if (s.emit) {
        const InputEmit::Counts e = s.emit->sample().value;
        out.rt_rounds = e.rt_rounds;
        out.rt_errors = e.rt_errors;
        out.edge_resets = e.edge_resets;
    }
    if (s.wire) {
        out.gates = s.wire->gates();
        out.ui_keys = s.wire->ui_keys_published();
        out.ui_keys_dropped = s.wire->ui_keys_dropped();
        out.keys_dropped = s.wire->keys_dropped();
        out.key_sweeps = s.wire->key_sweeps();
        out.keys_released = s.wire->keys_released();
        out.rebuilds_done = s.wire->rebuilds_done();
        out.rebuilds_refused = s.wire->rebuilds_refused();
        out.button_actions = s.wire->button_actions();
    }
    return out;
}

}
