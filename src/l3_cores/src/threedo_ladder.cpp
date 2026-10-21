// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/threedo_ladder.h"

namespace mister::cores {
namespace {

constexpr std::uint8_t kKanjiGroup = 1;

}

Ex<bool> ThreeDoLadder::on_step() {
    TASTY_SEAT_BODY(ThreeDoLadder);
    auto r = rung();
    if (!r && bit0_asserted_) {
        bit0_asserted_ = false;
        (void)order_status_bit0(false);
    }
    return r;
}

Ex<bool> ThreeDoLadder::rung() {
    switch (rung_) {
        case Rung::Unload:

            if (mount_bounded(std::string_view{}) == MountState::Pending)
                return false;
            report_.same_game = false;
            report_.bios_found = false;
            rung_ = Rung::Detach;
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
                    rung_ = Rung::Kanji;
                    return false;
                }
                return false;
            }

            ++report_.bios_missing;
            if (group_gates(0)) {
                rung_ = Rung::ResetRelease;
                return false;
            }
            rung_ = Rung::Kanji;
            return false;
        }

        case Rung::Kanji: {

            const auto rows = profile_.boot_assets;
            if (report_.bios_found) {
                while (row_i_ < rows.size() && rows[row_i_].group != kKanjiGroup)
                    ++row_i_;
                if (row_i_ < rows.size()) {
                    auto hit = load_row(rows[row_i_]);
                    if (!hit)
                        return std::unexpected(hit.error());
                    if (*hit == AssetWait::Pending)
                        return false;
                    ++row_i_;
                    if (*hit == AssetWait::Ready)
                        rung_ = Rung::MountDisc;
                    return false;
                }
            }
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
            rung_ = Rung::DiscInfo;
            return false;
        }

        case Rung::DiscInfo:

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
