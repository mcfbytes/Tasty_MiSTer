// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_ladder.h"

#include "proto/types.h"

namespace mister::cores {
namespace {

std::string_view cdi_dir_of(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

}

CdiLadder::CdiLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx),
      last_dir_(ctx.last_dir),
      at_core_start_(ctx.at_core_start) {
    if (at_core_start_) {

        last_dir_.assign(profile.staging.start_save_dir);
        rung_ = Rung::SaveIndex;
    }
}

Ex<bool> CdiLadder::on_step() {
    TASTY_SEAT_BODY(CdiLadder);
    switch (rung_) {
        case Rung::MountDisc: {
            if (image_.empty()) {
                if (mount_bounded(std::string_view{}) == MountState::Pending)
                    return false;
                rung_ = Rung::Eject;
                return false;
            }
            const MountState ms = mount_bounded(image_);
            if (ms == MountState::Pending)
                return false;
            if (ms == MountState::Failed) {
                rung_ = Rung::Eject;
                return false;
            }
            report_.disc_mounted = true;

            const std::string_view cur = cdi_dir_of(image_);
            report_.same_game = !last_dir_.empty() && cur == last_dir_;
            if (!report_.same_game)
                last_dir_.assign(cur);
            rung_ = report_.same_game ? Rung::DiscIndex : Rung::SaveIndex;
            return false;
        }

        case Rung::SaveIndex:

            if (!order_set_index(WideIoIndex{profile_.staging.save_slot.v}))
                return false;
            rung_ = Rung::Save;
            return false;

        case Rung::Save: {
            if (last_dir_.empty()) {

                if (!order_mount_save(std::string_view{}, false))
                    return false;
                report_.save_detached = true;
            } else {
                if (save_path_.empty()) {

                    std::string dir = "saves/";
                    dir.append(profile_.name);
                    if (auto r = vfs_.ensure_dir(dir); !r)
                        return std::unexpected(r.error());
                    save_path_ = dir;
                    save_path_ += '/';
                    save_path_.append(base_of(last_dir_));
                    save_path_ += ".sav";
                }
                if (!prepare_save(save_path_))
                    return false;
                if (!order_mount_save(save_path_, false))
                    return false;
                report_.save_mounted = true;
            }
            if (at_core_start_) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::DiscIndex;
            return false;
        }

        case Rung::DiscIndex:

            if (!order_set_index(WideIoIndex{profile_.staging.disc_slot.v}))
                return false;
            rung_ = Rung::Announce;
            return false;

        case Rung::Announce:
            if (!order_announce_disc(mount_answer().disc_size_bytes))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Eject:
            if (!order_announce_disc(0))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
