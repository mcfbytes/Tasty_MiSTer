// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/stick_level_emitter.h"

namespace mister::app::input {

void StickLevelEmitter::publish(bool muted, svc::InputGates gates) {

    const infra::Tally binds = pads_.binds();
    const infra::Tally gate_ends = pads_.gate_ends();
    if (reset_seen_.take(edge_reset_) || binds != binds_seen_)
        forget_(proto::StickPos{});
    if (gate_ends != gate_ends_seen_)
        forget_(std::nullopt);
    binds_seen_ = binds;
    gate_ends_seen_ = gate_ends;
    if (muted || !gates.grabbed || gates.osd_visible)
        return;
    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        const svc::PlayerIndex p{i};
        PlayerSticks& sticks = levels_[p];
        for (const svc::input::StickIndex s : svc::input::kSticks) {
            const proto::StickPos pos = svc::input::stick_pos(svc_.stick(p, s));
            if (sticks.posted[s] == pos)
                continue;
            const auto side = s.side();
            if (!side || !pads_.stick(p, *side, pos))
                continue;
            sticks.posted[s] = pos;
            activity_.add();
        }
    }
}

void StickLevelEmitter::forget_(std::optional<proto::StickPos> at) noexcept {
    for (PlayerSticks& sticks : levels_)
        for (std::optional<proto::StickPos>& posted : sticks.posted)
            posted = at;
}

}
