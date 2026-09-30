// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_pipeline.h"

#include <type_traits>

#include "app/link_tx_channel.h"

namespace mister::app {

static_assert(std::is_constructible_v<InputPipeline, const svc::Vfs&, hal::ISpiTransport&,
                                      os::IClock&, proto::ILinkRouter&>,
              "the intended constructor must still exist");

InputPipeline::InputPipeline(const svc::Vfs& vfs, hal::ISpiTransport& link, os::IClock& clock,
                             proto::ILinkRouter& router) noexcept
    : decode_(wire_, clock), build_(vfs, wire_), emit_(wire_, link, router) {}

Ex<void> InputPipeline::open() {
    if (auto b = build_.open(); !b) return std::unexpected(b.error());

    emit_.arm();
    if (auto c = wire_.open_ctrl(); !c) return std::unexpected(c.error());
    if (auto o = decode_.open(); !o) return std::unexpected(o.error());
    return decode_.arm(build_.service());
}

void InputEmit::on_rt_round(bool tick, std::uint32_t core_edge_seq, bool live) {

    TASTY_SEAT_BODY(InputEmit);
    if (!armed_) return;
    InputWire::bump(n_rt_rounds_, 1);

    const std::uint32_t gates = wire_.gates();
    const bool grabbed = (gates & InputWire::kGateGrabbed) != 0u;
    const bool osd = (gates & InputWire::kGateOsdVisible) != 0u;
    em_.set_gates(grabbed, osd);

    if (core_edge_seq != wm_core_edge_seq_) {
        wm_core_edge_seq_ = core_edge_seq;
        ps2_.forget();
    }

    if (!live) return;
    if (tick) ps2_.probe();
    ps2_.service();
}

void InputEmit::apply_edge_reset() {
    TASTY_SEAT_BODY(InputEmit);
    if (!armed_) return;
    if (const std::uint32_t rs = wire_.edge_reset_seq(); rs != wm_reset_seq_) {
        wm_reset_seq_ = rs;
        em_.joysticks().reset_edges();
        InputWire::bump(n_edge_reset_, 1);
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

InputBuild::InputBuild(const svc::Vfs& vfs, InputWire& wire) noexcept : vfs_(&vfs), wire_(wire) {

    if (auto svc = svc::InputService::create(); svc) {
        svc_.emplace(std::move(*svc));
    }
}

void InputBuild::set_input_dir(std::string_view dir) { input_dir_.assign(dir); }

void InputBuild::set_cfg_deadzone_rules(std::span<const svc::DeadzoneRule> rows) {
    if (svc_) svc_->set_cfg_deadzone_rules(rows);
}

Ex<void> InputBuild::open() {
    if (!svc_) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    opened_ = true;

    svc_->bind_storage(*vfs_);
    if (!input_dir_.empty()) svc_->set_input_dir(input_dir_);

    if (auto w = svc_->open_hotplug_watch(); !w) {
        InputWire::bump(n_enum_fail_, 1);
    }
    if (auto e = svc_->enumerate(); !e) {
        InputWire::bump(n_enum_fail_, 1);
    }

    (void)svc_->load_maps_for(std::string_view{}, false);

    wire_.publish_gates(svc_->grabbed() ? InputWire::kGateGrabbed : 0u, InputWire::kGateOsdVisible);
    return {};
}

bool InputBuild::rebind_round(unsigned wait_ms) {

    TASTY_SEAT_BODY(InputBuild);
    if (!opened_) return false;

    if ((wire_.requests() & InputWire::kReqHotplug) == 0u) return false;
    wire_.clear_request(InputWire::kReqHotplug);

    for (unsigned i = 0; i < wait_ms * 10u && wire_.paused(); ++i) {
        timespec ts{0, 100'000};
        (void)::nanosleep(&ts, nullptr);
    }
    if (wire_.paused()) {
        wire_.request(InputWire::kReqHotplug);
        wire_.note_rebuild_refused();
        return false;
    }

    const std::uint32_t seq0 = wire_.pause_seq();

    wire_.request(InputWire::kReqRebuild);

    wire_.wake();

    bool paused = false;
    for (unsigned i = 0; i < wait_ms * 10u && !paused; ++i) {
        paused = wire_.paused() && wire_.pause_seq() != seq0;
        if (!paused) {
            timespec ts{0, 100'000};
            (void)::nanosleep(&ts, nullptr);
        }
    }
    if (!paused) {

        wire_.clear_request(InputWire::kReqRebuild);
        wire_.request(InputWire::kReqHotplug);
        wire_.note_rebuild_refused();
        return false;
    }

    if (auto e = svc_->enumerate(); !e) {
        InputWire::bump(n_enum_fail_, 1);
    }

    {

        const InputWire::CoreNameCell core = wire_.core_name();

        svc_->set_joy_plan(wire_.joy_plan());
        svc_->set_analog_reshape(wire_.analog_reshape());
        (void)svc_->load_maps_for(core.view(), core.front_end);
    }
    wire_.clear_request(InputWire::kReqRebuild);
    wire_.note_rebuild_done();
    wire_.wake();
    return true;
}

InputBuild::Counts InputBuild::counts() const noexcept {
    Counts c{};
    c.enumerate_failures = n_enum_fail_.load(std::memory_order_relaxed);
    if (!opened_) return c;
    c.devices = svc_->stats().devices;
    c.mapped = svc_->mapped_devices();
    c.slotted = svc_->slotted_devices();
    c.mask_changes = svc_->mask_recomputes();
    c.axis_edges = svc_->axis_edges();
    c.mouse_remainder = svc_->mouse_remainder_bytes();
    c.key_overflows = svc_->key_overflows();
    c.quirk_drops = svc_->quirk_drops();
    c.rejected = svc_->rejected_nodes();
    c.slot_live = svc_->live_slot_mask();
    c.slot_ghosts = svc_->ghost_slots();
    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        c.slot_hash[i] = svc_->slot_key_hash(i);
    }
    return c;
}

InputStats collect_input_stats(const InputSample& s) noexcept {
    InputStats out{};
    if (s.decode != nullptr) {
        const InputDecode::Counts d = s.decode->counts();
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
    }
    if (s.build != nullptr) {
        const InputBuild::Counts b = s.build->counts();
        out.enumerate_failures = b.enumerate_failures;
        out.devices = b.devices;
        out.mapped = b.mapped;
        out.slotted = b.slotted;
        out.mask_changes = b.mask_changes;
        out.axis_edges = b.axis_edges;
        out.mouse_remainder = b.mouse_remainder;
        out.key_overflows = b.key_overflows;
        out.quirk_drops = b.quirk_drops;
        out.rejected = b.rejected;
        out.slot_live = b.slot_live;
        out.slot_ghosts = b.slot_ghosts;
        for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
            out.slot_hash[i] = b.slot_hash[i];
        }
    }
    if (s.emit != nullptr) {
        const InputEmit::Counts e = s.emit->counts();
        out.rt_rounds = e.rt_rounds;
        out.joy_transactions = e.joy_transactions;
        out.rt_errors = e.rt_errors;
        out.edge_resets = e.edge_resets;
    }
    if (s.wire != nullptr) {
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
