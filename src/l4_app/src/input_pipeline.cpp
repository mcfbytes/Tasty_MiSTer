// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_pipeline.h"

#include <type_traits>
#include <utility>

#include "app/link_tx_channel.h"

namespace mister::app {

static_assert(std::is_constructible_v<InputPipeline, InputDecode::Fds, InputWire&, InputBuild&,
                                      InputEmit&, os::IClock&, InputDecode::Wiring>,
              "the intended constructor must still exist");
static_assert(!std::is_default_constructible_v<InputPipeline>,
              "an unopened pipeline must have no representation");

InputPipeline::InputPipeline(InputDecode::Fds fds, InputWire& wire, InputBuild& build,
                             InputEmit& emit, os::IClock& clock,
                             InputDecode::Wiring decode_wiring) noexcept
    : wire_(wire), build_(build), emit_(emit), service_(open_service_(build)),
      decode_(std::move(fds), wire, service_, build.tables(), clock, decode_wiring),
      sample_{infra::OptRef<const InputDecode::CountsCell>{decode_.counts_cell()},
              infra::OptRef<const InputEmit::CountsCell>{emit_.counts_cell()},
              infra::OptRef<const InputWire>{wire_}} {}

svc::InputService InputPipeline::open_service_(InputBuild& build) noexcept {
    svc::InputService s = svc::InputService::create();
    if (auto w = s.open_hotplug_watch(build.enumerator().input_dir()); !w) {
        build.note_watch_failure();
    }
    return s;
}

void InputPipeline::latch_cfg(std::span<const svc::DeadzoneRule> deadzones, bool rumble) noexcept {
    service_.apply_deadzone_rules(deadzones);
    service_.set_cfg_rumble(rumble);
}

void InputEmit::on_rt_round(bool tick, std::uint32_t core_edge_seq, bool live) {

    TASTY_SEAT_BODY(InputEmit);
    InputWire::bump(n_rt_rounds_, 1);

    const std::uint32_t gates = wire_.gates();
    const bool grabbed = (gates & InputWire::kGateGrabbed) != 0u;
    const bool osd = (gates & InputWire::kGateOsdVisible) != 0u;
    em_.set_gates(grabbed, osd);

    if (core_edge_seq != wm_core_edge_seq_) {
        wm_core_edge_seq_ = core_edge_seq;
        ps2_.forget();
    }

    if (live) {
        if (tick) ps2_.probe();
        ps2_.service();
    }
    counts_cell_.publish(counts());
}

void InputEmit::apply_edge_reset() {
    TASTY_SEAT_BODY(InputEmit);
    if (const std::uint32_t rs = wire_.edge_reset_seq(); rs != wm_reset_seq_) {
        wm_reset_seq_ = rs;
        em_.joysticks().reset_edges();
        InputWire::bump(n_edge_reset_, 1);
        counts_cell_.publish(counts());
    }
}

InputEmit::Counts InputEmit::counts() const noexcept {
    Counts c{};
    c.rt_rounds = n_rt_rounds_.load(std::memory_order_relaxed);
    c.joy_transactions = n_joy_tx_.load(std::memory_order_relaxed);
    c.rt_errors = ps2_.errors();
    c.edge_resets = n_edge_reset_.load(std::memory_order_relaxed);
    return c;
}

InputBuild::InputBuild(const svc::Vfs& vfs, InputWire& wire, std::string_view input_dir)
    : wire_(wire), en_(svc::InputEnumerator::create(vfs)) {
    if (!input_dir.empty()) en_.set_input_dir(input_dir);

    if (auto e = en_.enumerate(); !e) {
        InputWire::bump(n_enum_fail_, 1);
    }

    (void)en_.load_maps_for(std::string_view{}, false);

    (void)hand_over_();

    wire_.publish_gates(svc::InputService::kBornGrabbed ? InputWire::kGateGrabbed : 0u,
                        InputWire::kGateOsdVisible);
}

void InputBuild::set_cfg_deadzone_rules(std::span<const svc::DeadzoneRule> rows) {
    en_.set_cfg_deadzone_rules(rows);
}

bool InputBuild::hand_over_() {
    svc::DeviceTable t = en_.take_table();
    return tx_.send(t).has_value();
}

bool InputBuild::rebind_round() {

    TASTY_SEAT_BODY(InputBuild);
    tx_.reap();
    if ((wire_.requests() & InputWire::kReqHotplug) == 0u) return false;

    if (!tx_.ready()) {
        wire_.note_rebuild_refused();
        return false;
    }

    wire_.clear_request(InputWire::kReqHotplug);

    if (auto e = en_.enumerate(); !e) {
        InputWire::bump(n_enum_fail_, 1);
    }
    {

        const InputWire::CoreNameCell core = wire_.core_name();

        en_.set_joy_plan(wire_.joy_plan());
        const svc::IAnalogReshape* r = wire_.analog_reshape();
        const auto reshape = infra::opt_ref_of(r);
        (void)en_.load_maps_for(core.view(), core.front_end, reshape);
    }
    if (!hand_over_()) {
        wire_.request(InputWire::kReqHotplug);
        wire_.note_rebuild_refused();
        return false;
    }
    wire_.note_rebuild_done();
    wire_.wake();
    return true;
}

InputBuild::Counts InputBuild::counts() const noexcept {
    Counts c{};
    c.enumerate_failures = n_enum_fail_.load(std::memory_order_relaxed);
    c.devices = en_.stats().devices;
    c.rejected = en_.rejected_nodes();
    return c;
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
        out.mapped = d.mapped;
        out.slotted = d.slotted;
        out.mask_changes = d.mask_changes;
        out.axis_edges = d.axis_edges;
        out.mouse_remainder = d.mouse_remainder;
        out.key_overflows = d.key_overflows;
        out.quirk_drops = d.quirk_drops;
        out.slot_live = d.slot_live;
        out.slot_ghosts = d.slot_ghosts;
        for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
            out.slot_hash[i] = d.slot_hash[i];
        }
    }
    out.enumerate_failures = b.enumerate_failures;
    out.devices = b.devices;
    out.rejected = b.rejected;
    if (s.emit) {
        const InputEmit::Counts e = s.emit->sample().value;
        out.rt_rounds = e.rt_rounds;
        out.joy_transactions = e.joy_transactions;
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
        out.button_samples = s.wire->button_samples();
        out.button_actions = s.wire->button_actions();
    }
    return out;
}

}  // namespace mister::app
