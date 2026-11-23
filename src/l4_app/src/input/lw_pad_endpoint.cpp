// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/lw_pad_endpoint.h"

#include <cstddef>

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
    seen_ = {};
    gated_ = false;
}

void LwPadEndpoint::serve_bound_() noexcept {
    const bool moved = apply_relay_(false);
    if (pads_->owes()) {
        store_();
        return;
    }
    if (moved || unpublished_)
        publish_();
}

bool LwPadEndpoint::idle_bound_hook_() const noexcept {
    return !pads_ || !pads_->owes();
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
    pads_->settle();
    pads_->open(false);
    tally_.stores = stores_before_.sum(pads_->stores());
    tally_.events_refused = refused_before_.sum(pads_->refused());
    release_();
}

LwPadEndpoint::Port& LwPadEndpoint::port_() noexcept {
    if (w_.port)
        return *w_.port;
    return *block_;
}

}
