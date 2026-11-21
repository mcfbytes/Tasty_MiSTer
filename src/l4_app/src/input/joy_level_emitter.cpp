// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/joy_level_emitter.h"

namespace mister::app::input {

void JoyLevelEmitter::publish(bool muted, svc::InputGates gates) {

    svc::JoyMask seen{};
    svc::PlayerMask players{};

    const infra::Tally binds = pads_.binds();
    const bool rebound = binds != binds_seen_;
    binds_seen_ = binds;
    if (reset_seen_.take(edge_reset_) || rebound) {
        for (PlayerLevel& level : levels_)
            level.posted = {};
    }
    wm_gates_ = gates;
    const bool grabbed = gates.grabbed;
    const bool osd_visible = gates.osd_visible;

    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        const svc::PlayerIndex p{i};
        PlayerLevel& level = levels_[p];
        const svc::JoyMask mask = svc_.joy_mask(p);
        if (!mask.empty()) {
            seen = seen.with(mask);
            players = players.with(p);
        }

        if (mask != level.armed_mask || level.autofire != level.armed_autofire) {
            level.armed_mask = mask;
            level.armed_autofire = level.autofire;
            level.zero_armed = true;
        }
        if (!muted && grabbed && mask != level.posted) {

            if (pads_.emit(p, proto::PadMasks{mask, level.autofire})) {
                level.posted = mask;
                activity_.add();
            }
        }
    }

    if (!grabbed || osd_visible) {
        for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
            PlayerLevel& level = levels_[svc::PlayerIndex{i}];
            if (!level.zero_armed)
                continue;
            const svc::JoyMask tested = grabbed ? level.armed_mask.with(level.autofire)
                                                : level.armed_mask;
            if (!muted && pads_.release(svc::PlayerIndex{i}, tested))
                activity_.add();

            level.zero_armed = false;
        }
    }
    tally_.merge_joy(seen, players);
}

}
