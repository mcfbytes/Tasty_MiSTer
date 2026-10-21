// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/saturn_ladder.h"

#include "proto/types.h"

namespace mister::cores {
namespace {

constexpr proto::StatusBit kNoResetBit{4};

}

Ex<bool> SaturnLadder::on_step() {
    TASTY_SEAT_BODY(SaturnLadder);
    auto r = rung();
    if (!r && bit0_asserted_) {
        bit0_asserted_ = false;
        (void)order_status_bit0(false);
    }
    return r;
}

Ex<bool> SaturnLadder::rung() {
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

            if (!order_status_bit0(true))
                return false;
            bit0_asserted_ = true;
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

            if (image_.empty()) {
                rung_ = Rung::ResetRelease;
                return false;
            }
            const MountState ms = mount_bounded(image_);
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
            rung_ = Rung::BootHeader;
            return false;
        }

        case Rung::BootHeader:

            if (!order_disc_payload())
                return false;
            rung_ = Rung::ResetRelease;
            return false;

        case Rung::ResetRelease:

            if (!order_status_bit0(false))
                return false;
            bit0_asserted_ = false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
