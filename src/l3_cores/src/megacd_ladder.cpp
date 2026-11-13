// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megacd_ladder.h"

namespace mister::cores {

Ex<bool> MegaCdLadder::on_step() {
    TASTY_SEAT_BODY(MegaCdLadder);
    switch (rung_) {
        case Rung::Unload:

            if (mount_bounded(std::string_view{}) == MountState::Pending)
                return false;
            rung_ = Rung::Detach;
            return false;

        case Rung::Detach:

            if (report_.same_game) {
                rung_ = Rung::MountDisc;
                return false;
            }

            if (!order_mount_save(std::string_view{}, true))
                return false;
            report_.save_detached = true;
            rung_ = Rung::PulseAssert;
            return false;

        case Rung::PulseAssert: {

            const std::chrono::microseconds pulse{profile_.staging.reset_pulse_us};
            if (!hold_reset_())
                return false;
            if (pulse == std::chrono::microseconds::zero() && !release_reset_())
                return false;
            rung_ = Rung::PulseRelease;
            return false;
        }

        case Rung::PulseRelease:

            if (!pulse_reset_(std::chrono::microseconds{profile_.staging.reset_pulse_us}))
                return false;

            if (!order_reset())
                return false;
            rung_ = Rung::Bios;
            row_.rewind();
            return false;

        case Rung::Bios: {

            while (const auto at = row_.current_in(BootGroup::First)) {
                const BootAsset& row = *at;
                auto hit = load_row(row);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                row_.advance();
                if (*hit == AssetWait::Ready) {
                    report_.bios_found = true;
                    rung_ = Rung::MountDisc;
                    return false;
                }
                return false;
            }

            ++report_.bios_missing;
            if (group_gates(BootGroup::First)) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::MountDisc;
            return false;
        }

        case Rung::MountDisc: {

            if (image_path().empty()) {
                rung_ = Rung::Done;
                return true;
            }
            const MountState ms = mount_bounded(image_path());
            if (ms == MountState::Pending)
                return false;
            if (ms == MountState::Failed) {

                rung_ = Rung::Done;
                return true;
            }
            report_.disc_mounted = true;
            if (report_.same_game) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::LayeredBios;
            return false;
        }

        case Rung::LayeredBios: {

            row_.skip_group(BootGroup::First);
            if (const auto at = row_.current()) {
                auto hit = load_row(*at);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                row_.advance();
                return false;
            }
            rung_ = Rung::MountSave;
            return false;
        }

        case Rung::MountSave: {

            auto m = mount_save_rung_();
            if (!m)
                return std::unexpected(m.error());
            if (!*m)
                return false;
            rung_ = Rung::Done;
            return true;
        }

        case Rung::Done:
            return true;
    }
    return true;
}

}
