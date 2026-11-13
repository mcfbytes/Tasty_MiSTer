// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/threedo_ladder.h"

namespace mister::cores {
namespace {

constexpr BootGroup kKanjiGroup = BootGroup::Second;

}

Ex<bool> ThreeDoLadder::on_step() {
    TASTY_SEAT_BODY(ThreeDoLadder);
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

            if (!hold_reset_())
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
                    rung_ = Rung::Kanji;
                    return false;
                }
                return false;
            }

            ++report_.bios_missing;
            if (group_gates(BootGroup::First)) {
                rung_ = Rung::ResetRelease;
                return false;
            }
            rung_ = Rung::Kanji;
            return false;
        }

        case Rung::Kanji: {

            if (report_.bios_found) {
                row_.skip_to_group(kKanjiGroup);
                if (const auto at = row_.current()) {
                    auto hit = load_row(*at);
                    if (!hit)
                        return std::unexpected(hit.error());
                    if (*hit == AssetWait::Pending)
                        return false;
                    row_.advance();
                    if (*hit == AssetWait::Ready)
                        rung_ = Rung::MountDisc;
                    return false;
                }
            }
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
            rung_ = Rung::MountSave;
            return false;
        }

        case Rung::MountSave: {

            auto m = mount_save_rung_();
            if (!m)
                return std::unexpected(m.error());
            if (!*m)
                return false;
            rung_ = Rung::DiscInfo;
            return false;
        }

        case Rung::DiscInfo:

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
