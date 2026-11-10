// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/staging_core.h"

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
            rung_ = image_path().empty() ? Rung::Reset : Rung::Walk;
            return false;

        case Rung::Walk:

            if (!walk_answered_())
                return false;
            rung_ = Rung::MountDisc;
            return false;

        case Rung::MountDisc: {

            const MountState ms = mount_bounded(image_path());
            if (ms == MountState::Pending)
                return false;
            report_.disc_mounted = ms == MountState::Done;
            rung_ = Rung::Reset;
            [[fallthrough]];
        }

        case Rung::Reset:

            if (!order_reset())
                return false;
            if (image_path().empty()) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::Release;
            [[fallthrough]];

        case Rung::Release:

            if (!release_reset_())
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

bool NeoGeoCdLadder::walk_answered_() {
    if (!walk_gen_)
        walk_gen_ = next_gen();
    if (!walk_ordered_) {
        if (host_.order_walk(profile_.staging.disc_slot, image_path(), walk_gen_)) {
            walk_ordered_ = true;
            return false;
        }
        const std::chrono::nanoseconds now = now_ns();
        if (refused_since_ < std::chrono::nanoseconds::zero())
            refused_since_ = now;
        return now - refused_since_ >= kDiscMountBoundNs;
    }
    const MountStatus s = host_.walk_status();
    return s.generation == walk_gen_ && s.state != MountState::Pending;
}

}
