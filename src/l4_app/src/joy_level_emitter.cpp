// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/joy_level_emitter.h"

#include "app/link_tx_channel.h"

namespace mister::app {

void JoyLevelEmitter::publish(bool muted) {

    std::uint32_t seen = tally_.joy_seen();
    std::uint32_t players = tally_.joy_players();

    if (reset_seen_.take(wire_.edge_reset())) {
        for (PlayerLevel& level : levels_)
            level.posted = {};
    }
    wm_gates_ = wire_.gates();
    const bool grabbed = InputWire::has(wm_gates_, InputWire::kGateGrabbed);
    const bool osd_visible = InputWire::has(wm_gates_, InputWire::kGateOsdVisible);

    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        const svc::PlayerIndex p{i};
        PlayerLevel& level = levels_[p];
        const svc::JoyMask mask = svc_.joy_mask(p);
        if (mask.v != 0u) {
            seen |= mask.v;
            players |= (1u << i);
        }
        level.autofire = wire_.joy(p).autofire;

        wire_.publish_joy(p, mask, wire_.joy(p).autofire);
        if (mask != level.armed_mask || level.autofire != level.armed_autofire) {
            level.armed_mask = mask;
            level.armed_autofire = level.autofire;
            level.zero_armed = true;
        }
        if (!muted && grabbed && mask != level.posted) {

            if (inbox_.push(
                    proto::LinkOp::JoyEmit{
                        .player = proto::PlayerIndex{i},
                        .mask = mask,
                        .autofire = level.autofire
                    }
                )) {
                level.posted = mask;
                wire_.note_activity();
            }
        }
    }

    if (!grabbed || osd_visible) {
        for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
            PlayerLevel& level = levels_[svc::PlayerIndex{i}];
            if (!level.zero_armed)
                continue;
            const proto::LinkOp::JoyRelease rel{
                .player = proto::PlayerIndex{i},
                .mask = grabbed ? level.armed_mask | level.autofire : level.armed_mask
            };
            if (!muted && inbox_.push(rel))
                wire_.note_activity();

            level.zero_armed = false;
        }
    }
    tally_.store_joy(seen, players);
}

}
