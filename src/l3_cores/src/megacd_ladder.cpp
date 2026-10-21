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

        case Rung::PulseAssert:

            if (!bit0_asserted_) {
                if (!order_status_bit0(true))
                    return false;
                bit0_asserted_ = true;
                pulse_release_ns_ = now_ns()
                    + static_cast<std::int64_t>(profile_.staging.reset_pulse_us) * 1000;
            }
            if (profile_.staging.reset_pulse_us == 0) {
                if (!order_status_bit0(false))
                    return false;
                bit0_asserted_ = false;
            }
            rung_ = Rung::PulseRelease;
            return false;

        case Rung::PulseRelease:

            if (bit0_asserted_) {
                if (now_ns() < pulse_release_ns_)
                    return false;
                if (!order_status_bit0(false))
                    return false;
                bit0_asserted_ = false;
            }

            if (!order_reset())
                return false;
            rung_ = Rung::Bios;
            row_i_ = 0;
            return false;

        case Rung::Bios: {

            const auto rows = profile_.boot_assets;
            while (row_i_ < rows.size() && rows[row_i_].group == 0) {
                const BootAsset& row = rows[row_i_];
                auto hit = load_row(row);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                ++row_i_;
                if (*hit == AssetWait::Ready) {
                    report_.bios_found = true;
                    rung_ = Rung::MountDisc;
                    return false;
                }
                return false;
            }

            ++report_.bios_missing;
            if (group_gates(0)) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::MountDisc;
            return false;
        }

        case Rung::MountDisc: {

            if (image_.empty()) {
                rung_ = Rung::Done;
                return true;
            }
            const MountState ms = mount_bounded(image_);
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

            const auto rows = profile_.boot_assets;
            while (row_i_ < rows.size() && rows[row_i_].group == 0)
                ++row_i_;
            if (row_i_ < rows.size()) {
                auto hit = load_row(rows[row_i_]);
                if (!hit)
                    return std::unexpected(hit.error());
                if (*hit == AssetWait::Pending)
                    return false;
                ++row_i_;
                return false;
            }
            rung_ = Rung::MountSave;
            return false;
        }

        case Rung::MountSave: {

            if (save_path_.empty()) {
                auto sp = save_path_for(image_);
                if (!sp)
                    return std::unexpected(sp.error());
                save_path_ = *sp;
            }
            if (!prepare_save(save_path_))
                return false;
            if (!order_mount_save(save_path_, true))
                return false;
            report_.save_mounted = true;
            rung_ = Rung::Done;
            return true;
        }

        case Rung::Done:
            return true;
    }
    return true;
}

}
