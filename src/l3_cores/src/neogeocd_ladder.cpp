// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeocd_ladder.h"

namespace mister::cores {

NeoGeoCdLadder::NeoGeoCdLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx) {
    report_.same_game = false;
    report_.bios_found = false;
}

Ex<bool> NeoGeoCdLadder::on_step() {
    TASTY_SEAT_BODY(NeoGeoCdLadder);
    switch (rung_) {
        case Rung::Unload:

            if (mount_bounded(std::string_view{}) == MountState::Pending)
                return false;
            rung_ = image_.empty() ? Rung::Reset : Rung::Walk;
            return false;

        case Rung::Walk:

            if (!walk_answered_())
                return false;
            rung_ = Rung::MountDisc;
            return false;

        case Rung::MountDisc: {

            const MountState ms = mount_bounded(image_);
            if (ms == MountState::Pending)
                return false;
            report_.disc_mounted = ms == MountState::Done;
            rung_ = Rung::Reset;
            [[fallthrough]];
        }

        case Rung::Reset:

            if (!order_reset())
                return false;
            if (image_.empty()) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::Release;
            [[fallthrough]];

        case Rung::Release:

            if (!order_status_bit0(false))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

bool NeoGeoCdLadder::walk_answered_() {
    if (walk_gen_ == 0)
        walk_gen_ = next_gen();
    if (!walk_ordered_) {
        if (host_.order_walk(profile_.staging.disc_slot, image_, walk_gen_)) {
            walk_ordered_ = true;
            return false;
        }
        const std::int64_t now = now_ns();
        if (refused_since_ < 0)
            refused_since_ = now;
        return now - refused_since_ >= kDiscMountBoundNs;
    }
    const MountStatus s = host_.walk_status();
    return s.generation == walk_gen_ && s.state != MountState::Pending;
}

}
