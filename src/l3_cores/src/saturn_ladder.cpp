// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/saturn_ladder.h"

#include "proto/types.h"

namespace mister::cores {
namespace {

constexpr proto::StatusBit kNoResetBit{4};

}

Ex<bool> SaturnLadder::on_step() {
    TASTY_SEAT_BODY(SaturnLadder);
    switch (rung_) {
        case Rung::Unload:

            if (mount_bounded(std::string_view{}) == MountState::Pending)
                return false;
            stage_ = !report_.same_game && !host_.status_word().get_bit(kNoResetBit);
            report_.bios_found = false;
            rung_ = stage_ ? Rung::Detach : Rung::MountDisc;
            return false;

        case Rung::Detach:

            if (!order_mount_save(std::string_view{}, true))
                return false;
            report_.save_detached = true;
            rung_ = Rung::ResetHold;
            return false;

        case Rung::ResetHold:

            if (!hold_reset_())
                return false;
            rung_ = Rung::Bios;
            row_i_ = 0;
            return false;

        case Rung::Bios: {

            const auto rows = profile_.boot_assets;
            if (row_i_ < rows.size()) {
                auto hit = load_row(rows[row_i_]);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                ++row_i_;
                if (*hit == AssetWait::Ready) {
                    report_.bios_found = true;
                    rung_ = Rung::MountDisc;
                }
                return false;
            }
            ++report_.bios_missing;
            rung_ = Rung::MountDisc;
            return false;
        }

        case Rung::MountDisc: {

            if (image_path().empty()) {
                rung_ = Rung::ResetRelease;
                return false;
            }
            const MountState ms = mount_bounded(image_path());
            if (ms == MountState::Pending)
                return false;
            if (ms == MountState::Failed) {
                rung_ = Rung::ResetRelease;
                return false;
            }
            report_.disc_mounted = true;
            rung_ = stage_ ? Rung::MountSave : Rung::BootHeader;
            return false;
        }

        case Rung::MountSave: {

            auto m = mount_save_rung_();
            if (!m)
                return std::unexpected(m.error());
            if (!*m)
                return false;
            rung_ = Rung::BootHeader;
            return false;
        }

        case Rung::BootHeader:

            if (!order_disc_payload())
                return false;
            rung_ = Rung::ResetRelease;
            return false;

        case Rung::ResetRelease:

            if (!release_reset_())
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
