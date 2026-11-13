// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_ladder.h"

#include "infra/path_parts.h"
#include "proto/types.h"

namespace mister::cores {

CdiLadder::CdiLadder(const CoreProfile& profile, const LadderContext& ctx)
    : BootLadder(profile, ctx),
      last_dir_(ctx.last_dir) {
    if (ctx.kind() == LadderContext::ArmKind::CoreStart) {

        if (const std::optional<SaveMountPolicy>& save = profile.staging.save)
            last_dir_.assign(save->start_dir);
        rung_ = Rung::SaveIndex;
    }
}

Ex<bool> CdiLadder::on_step() {
    TASTY_SEAT_BODY(CdiLadder);
    switch (rung_) {
        case Rung::MountDisc: {
            if (image_path().empty()) {
                if (mount_bounded(std::string_view{}) == MountState::Pending)
                    return false;
                rung_ = Rung::Eject;
                return false;
            }
            const MountState ms = mount_bounded(image_path());
            if (ms == MountState::Pending)
                return false;
            if (ms == MountState::Failed) {
                rung_ = Rung::Eject;
                return false;
            }
            report_.disc_mounted = true;

            const std::string_view cur = infra::dir_or_empty(image_path());
            report_.same_game = !last_dir_.empty() && cur == last_dir_;
            if (!report_.same_game)
                last_dir_.assign(cur);
            rung_ = report_.same_game ? Rung::DiscIndex : Rung::SaveIndex;
            return false;
        }

        case Rung::SaveIndex:

            if (!profile_.staging.save
                || !order_set_index(WideIoIndex::of(profile_.staging.save->slot)))
                return false;
            rung_ = Rung::Save;
            return false;

        case Rung::Save: {
            if (last_dir_.empty()) {

                if (!order_mount_save(std::string_view{}, false))
                    return false;
                report_.save_detached = true;
            } else {

                if (auto r = resolve_dir_save_(last_dir_); !r)
                    return std::unexpected(r.error());
                if (!prepare_save(save_name()))
                    return false;
                if (!order_mount_save(save_name(), false))
                    return false;
                report_.save_mounted = true;
            }
            if (core_start_armed()) {
                rung_ = Rung::Done;
                return true;
            }
            rung_ = Rung::DiscIndex;
            return false;
        }

        case Rung::DiscIndex:

            if (!order_set_index(WideIoIndex::of(profile_.staging.disc_slot)))
                return false;
            rung_ = Rung::Announce;
            return false;

        case Rung::Announce:
            if (!order_announce_disc(mount_answer().plan.disc_size_bytes))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Eject:
            if (!order_announce_disc(proto::FileSize{}))
                return false;
            rung_ = Rung::Done;
            return true;

        case Rung::Done:
            return true;
    }
    return true;
}

}
