// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/lw_pad_endpoint.h"

#include <cstddef>

#include "hal/pad_stat.h"
#include "infra/prelude.h"
#include "os/timespec.h"
#include "proto/link/pad_ports.h"
#include "proto/stick_pos.h"
#include "proto/uio_opcodes.h"

namespace mister::app::input {

namespace {

using proto::link::PadLevels;
constexpr svc::PlayerIndex kPortLimit{proto::link::kPadPorts};

}

bool LwPadEndpoint::emit(svc::PlayerIndex player, proto::PadMasks masks) noexcept {
    TASTY_SEAT_BODY(LwPadEndpoint);
    if (!pads_ || !(player < kPortLimit))
        return false;

    if (gated_) {
        tally_.gated_drops.add();
        unpublished_ = true;
        return true;
    }
    set_level_(player, masks.composed());
    return true;
}

bool LwPadEndpoint::release(svc::PlayerIndex player, svc::JoyMask tested) noexcept {
    TASTY_SEAT_BODY(LwPadEndpoint);
    if (!pads_ || !(player < kPortLimit))
        return false;

    if (tested.empty())
        return false;
    if (gated_) {
        tally_.gated_drops.add();
        unpublished_ = true;
        return true;
    }
    set_level_(player, svc::JoyMask{});
    return true;
}

bool LwPadEndpoint::stick(
    svc::PlayerIndex player,
    proto::StickSide side,
    proto::StickPos pos
) noexcept {
    TASTY_SEAT_BODY(LwPadEndpoint);
    if (!pads_ || !(player < kPortLimit))
        return false;
    if (gated_) {
        tally_.gated_drops.add();
        unpublished_ = true;
        return true;
    }

    const proto::JoystickWire& wire = grant_.body.wire;
    if (!wire.analog_capable)
        return true;
    store_stick_(wire.port_of(player), side, pos);
    return true;
}

bool LwPadEndpoint::offer(const proto::Ps2Frame& f) noexcept {
    TASTY_SEAT_BODY(LwPadEndpoint);
    if (!keys_bound())
        return false;
    const std::optional<proto::link::PadEvent> e = proto::link::PadEvent::of(f);
    if (!e)
        return false;

    if (events_.full())
        drop_oldest_mouse_();
    if (!events_.push(*e)) {
        tally_.events_lost.add();
        return true;
    }
    if (e->mouse())
        ++queued_mice_;
    return true;
}

bool LwPadEndpoint::takes_event() const noexcept {
    TASTY_SEAT_BODY(LwPadEndpoint);
    return !events_.full() || queued_mice_ != 0;
}

void LwPadEndpoint::store_events() noexcept {
    TASTY_SEAT_BODY(LwPadEndpoint);
    if (keys_bound() && !events_.empty())
        store_();
}

std::optional<proto::PadWord> LwPadEndpoint::wanted(PadLevels::Slot s) const noexcept {
    if (!pads_)
        return std::nullopt;
    return pads_->wanted(s);
}

void LwPadEndpoint::set_level_(svc::PlayerIndex player, svc::JoyMask composed) noexcept {
    const proto::JoystickWire& wire = grant_.body.wire;
    const svc::PlayerIndex port = wire.port_of(player);
    (void)pads_->digital(proto::link::digital_opcode(port), composed);
    if (!wire.translates() || !wire.analog_capable)
        return;
    store_stick_(port, proto::StickSide::Left, proto::StickPos::of_directions(composed));
}

void LwPadEndpoint::store_stick_(
    svc::PlayerIndex port,
    proto::StickSide side,
    proto::StickPos pos
) noexcept {
    (void)pads_->stick(proto::link::stick_opcode(side), port.stick_wire_index(), pos);
}

void LwPadEndpoint::publish_hook_(const PadRoute& r) noexcept {
    w_.route.publish(r);
    unpublished_ = false;
}

void LwPadEndpoint::prepare_hook_(const PadGrant& g) noexcept {
    if (!w_.port)
        block_.emplace(hal::reg::Block::redeem(g.block));

    if (g.body.keys)
        stat_.emplace(hal::reg::Block::redeem(g.block.first(hal::PadIdent::kSize)));
    pads_.emplace(port_());
}

bool LwPadEndpoint::arm_hook_(const PadGrant&) noexcept {
    return true;
}

void LwPadEndpoint::bind_hook_(const PadGrant&) noexcept {
    (void)apply_relay_(true);
    pads_->restore();
    tally_.stores = stores_before_.sum(pads_->stores());
}

void LwPadEndpoint::bound_hook_() noexcept {
    tally_.binds.add();
    publish_();
}

void LwPadEndpoint::answer_hook_(proto::BuildGeneration) noexcept {}

void LwPadEndpoint::unbind_hook_() noexcept {
    if (pads_) {
        refused_before_ = refused_before_.sum(pads_->refused());
        stores_before_ = stores_before_.sum(pads_->stores());
    }
    pads_.reset();
    block_.reset();
    stat_.reset();

    while (events_.pop())
        tally_.events_lost.add();
    queued_mice_ = 0;
    held_ = 0;
    seen_ = {};
    gated_ = false;
}

void LwPadEndpoint::serve_bound_() noexcept {
    const bool moved = apply_relay_(false);
    if (pads_->owes() || !events_.empty()) {
        store_();
        return;
    }
    if (moved || unpublished_)
        publish_();
}

bool LwPadEndpoint::idle_bound_hook_() const noexcept {
    return !pads_ || (!pads_->owes() && events_.empty());
}

bool LwPadEndpoint::apply_relay_(bool binding) noexcept {
    const auto s = w_.relay.sample();
    if (!s || s.value.gen != grant_.gen)
        return false;
    const proto::link::PadRelay& r = s.value;
    if (gated_ && !r.gated)
        gate_ends_.add();
    gated_ = r.gated;
    for (std::size_t i = 0; i < PadLevels::kCount; ++i) {
        const PadLevels::Slot slot = PadLevels::slot_at(i);
        const proto::link::PadRelay::Level& level = r.levels[slot];
        if (level.writes == seen_[slot])
            continue;
        seen_[slot] = level.writes;

        if (binding && !r.gated)
            continue;
        pads_->put(slot, level.word);
        tally_.gated_stores.add();
    }
    const bool moved = r.seq != tally_.relay_seq;
    if (moved)
        tally_.relay_read.at = os::steady_now();
    tally_.relay_seq = r.seq;
    return moved;
}

void LwPadEndpoint::store_() noexcept {
    if (!lease_())
        return;
    pads_->open(true);
    drain_events_();
    pads_->settle();
    pads_->open(false);
    tally_.stores = stores_before_.sum(pads_->stores());
    tally_.events_refused = refused_before_.sum(pads_->refused());
    release_();
}

void LwPadEndpoint::drain_events_() noexcept {
    if (!stat_)
        return;
    hal::PadEntries room{};
    unsigned reads = 0;
    while (!events_.empty()) {
        const proto::link::PadEvent& e = events_.front();
        while (room < e.entries() && reads < kStatReads) {
            room = hal::PadStat::of(stat_->read(hal::PadIdent::Reg::Stat)).room();
            ++reads;
        }
        if (room < e.entries() || !pads_->event(e))
            break;
        room = room.minus(e.entries());
        if (e.mouse())
            --queued_mice_;
        if (held_ != 0)
            --held_;
        (void)events_.pop();
    }
    for (; held_ < events_.size(); ++held_)
        tally_.events_held.add();
}

void LwPadEndpoint::drop_oldest_mouse_() noexcept {
    bool dropped = false;
    const std::size_t n = events_.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::optional<proto::link::PadEvent> e = events_.pop();
        if (!e)
            break;
        if (!dropped && e->mouse()) {
            dropped = true;
            --queued_mice_;
            if (i < held_)
                --held_;
            tally_.events_lost.add();
            continue;
        }
        events_.push(*e);
    }
}

LwPadEndpoint::Port& LwPadEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

}
