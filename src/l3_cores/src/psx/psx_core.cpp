// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx/psx_core.h"
#include "cores/core_adopt.h"
#include "hal/selected.h"

#include <algorithm>
#include <cstring>

#include "cores/cd/cdwire.h"
#include "cores/cd/mount_answer.h"
#include "cores/mounted_path.h"
#include "cores/psx/psx_disc_rules.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/prelude.h"
#include "proto/image_bracket.h"
#include "proto/image_sink.h"
#include "svc/disc/toc_frame.h"

namespace mister::cores::psx {

Ex<void> PsxCore::do_adopt(const CoreAdopt&) {

    if (!host().discs)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    discs_ = host().discs;
    geom_ = discs_->geometry();
    return {};
}

MountState PsxCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(PsxCore);
    const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
    if (!m)
        return cd::mount_answer_of(m.error());
    if (path.empty())
        return MountState::Done;

    switch (refresh_disc_()) {
        case svc::disc::DiscMountState::Pending:
            return MountState::Pending;
        case svc::disc::DiscMountState::Mounted:
            return finish_mount_() ? MountState::Done : MountState::Pending;
        case svc::disc::DiscMountState::Failed:
        case svc::disc::DiscMountState::Idle:
            break;
    }
    return MountState::Failed;
}

void PsxCore::set_region(proto::RegionCode) {
    TASTY_SEAT_BODY(PsxCore);
}

Ex<void> PsxCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot != profile().staging.disc_slot)
        return {};
    if (!discs_) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }

    if (mount_seen_ == svc::disc::DiscMountState::Mounted && image_path_.view() == p.path) {
        return {};
    }

    if (p.path.empty()) {
        if (discs_->arm_mount(svc::disc::DiscOp::Unmount, cue_, std::string_view{})
            == svc::disc::DiscArm::Declined) {
            return std::unexpected(cd::mount_declined(ERR_SITE()));
        }
        clear_mount_();
        (void)refresh_disc_();
        return {};
    }

    if (!infra::iends_with(p.path, ".cue") && !infra::iends_with(p.path, ".chd")) {
        clear_mount_();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    const svc::disc::DiscArm
        arm = discs_->arm_mount(svc::disc::DiscOp::Mount, cue_, p.path, profile().disc_rules);
    if (arm == svc::disc::DiscArm::Declined)
        return std::unexpected(cd::mount_declined(ERR_SITE()));
    clear_mount_();
    (void)refresh_disc_();
    if (arm == svc::disc::DiscArm::Failed) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)image_path_.assign(p.path);
    (void)finish_mount_();
    return {};
}

void PsxCore::clear_mount_() noexcept {
    plan_ = StagePlan{};
    image_path_.clear();
}

svc::disc::DiscMountState PsxCore::refresh_disc_() noexcept {
    if (!discs_)
        return svc::disc::DiscMountState::Failed;
    const svc::disc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        geom_ = discs_->geometry();
    }
    return st;
}

bool PsxCore::finish_mount_() {
    if (refresh_disc_() != svc::disc::DiscMountState::Mounted)
        return false;

    const svc::disc::Toc& toc = geom_.toc;
    plan_.data_first_track = toc.last != svc::TrackCount{}
        && toc.tracks[svc::TrackIndex{}].type != svc::disc::TrackType::Cdda;
    plan_.game_id = geom_.identity.id;
    plan_.disc_size_bytes = kPsxDiscRules.block_size().image_bytes(toc.end);
    return true;
}

Ex<void> PsxCore::stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) {
    TASTY_SEAT_BODY(PsxCore);
    if (!geom_.mounted) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    const svc::disc::TocFrameMeta meta{geom_.identity.region, op.reset, op.libcrypt_mask};
    const auto frame = svc::disc::build_toc_frame(geom_.toc, meta);
    auto ds = proto::ImageBracket::open(image_sink(), proto::WideIoIndex{251});
    if (!ds)
        return std::unexpected(ds.error());
    if (auto w = ds->write(infra::byte_cast<const std::uint8_t>(std::span(frame))); !w) {
        return w;
    }
    return ds->end();
}

void PsxCore::service_kick() noexcept {
    TASTY_SEAT_BODY(PsxCore);

    if (discs_)
        counters_ = discs_->counters();

    if (copy_in_flight_()) {
        ++held_kicks_;
        return;
    }
    hal::Selected cs(host().link, hal::ChipSelect::Io);
    if (auto r = host().link.transfer(hal::LinkWord{cd::cdwire::kUioCdGet}); !r)
        ++wire_faults_;
    ++kicks_;
}

bool PsxCore::copy_in_flight_() const noexcept {
    return host().save_copy && host().save_copy->sample().value.writing;
}

proto::SlotAttributes PsxCore::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(PsxCore);
    return proto::SlotAttributes{};
}

proto::BlockGeometry PsxCore::geometry_for(
    proto::SlotIndex slot,
    proto::Lba,
    proto::BlockGeometry wire
) {
    TASTY_SEAT_BODY(PsxCore);
    if (profile().staging.disc_slot.block_slot() != slot)
        return wire;
    proto::BlockGeometry g{kPsxDiscRules.block_size(), proto::BlockCount{}};

    const std::uint32_t hunk = geom_.chd_hunk_bytes();
    if (hunk != 0 && hunk <= proto::kBlockStagingBytes) {
        g.window_blocks = kPsxDiscRules.block_size().blocks_in(proto::FileSize{hunk});
    }
    return g;
}

}
