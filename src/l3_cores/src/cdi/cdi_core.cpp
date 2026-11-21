// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi/cdi_core.h"

#include <cstring>

#include "cores/cdi/cdi_disc_rules.h"
#include "cores/core_adopt.h"
#include "cores/cd/mount_answer.h"
#include "cores/mounted_path.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/prelude.h"
#include "svc/disc/track.h"
#include "proto/block_geometry.h"

namespace mister::cores::cdi {
namespace {

constexpr HeadLba kBootLba{166};
constexpr proto::StatusBit kServoAudioCd{12};

}

Ex<void> CdiCore::do_adopt(const CoreAdopt& a) {
    if (!host().discs)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    session_ = infra::OptRef<proto::link::CoreSession>{a.session};
    discs_ = host().discs;
    geom_ = discs_->geometry();
    return {};
}

MountState CdiCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(CdiCore);
    const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
    if (!m)
        return cd::mount_answer_of(m.error());
    if (path.empty())
        return MountState::Done;
    switch (refresh_disc_()) {
        case svc::disc::DiscMountState::Pending:
            return MountState::Pending;
        case svc::disc::DiscMountState::Mounted:
            return finish_mount_();
        case svc::disc::DiscMountState::Failed:
        case svc::disc::DiscMountState::Idle:
            break;
    }
    return MountState::Failed;
}

Ex<void> CdiCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot != profile().staging.disc_slot)
        return {};
    if (!discs_)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});

    if (mount_seen_ == svc::disc::DiscMountState::Mounted && image_path_.view() == p.path)
        return {};
    if (p.path.empty()) {
        if (discs_->arm_mount(
                svc::disc::DiscOp::Unmount,
                infra::OptRef<const svc::disc::CuePolicy>{cue_},
                std::string_view{}
            )
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
    const svc::disc::DiscArm arm = discs_->arm_mount(
        svc::disc::DiscOp::Mount,
        infra::OptRef<const svc::disc::CuePolicy>{cue_},
        p.path,
        infra::OptRef<const svc::disc::IDiscRules>{cdi::rules_after(disc_type_)}
    );
    if (arm == svc::disc::DiscArm::Declined)
        return std::unexpected(cd::mount_declined(ERR_SITE()));
    clear_mount_();
    (void)refresh_disc_();
    if (arm == svc::disc::DiscArm::Failed) {
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)image_path_.assign(p.path);
    return {};
}

void CdiCore::clear_mount_() noexcept {
    probed_ = false;
    plan_ = StagePlan{};
    image_path_.clear();
}

svc::disc::DiscMountState CdiCore::refresh_disc_() noexcept {
    if (!discs_)
        return svc::disc::DiscMountState::Failed;
    const svc::disc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        geom_ = discs_->geometry();
    }
    return st;
}

MountState CdiCore::finish_mount_() {
    if (probed_)
        return MountState::Done;

    if (!geom_.mounted || geom_.toc.last == svc::TrackCount{})
        return MountState::Failed;
    if (probe_boot_() == MountState::Pending)
        return MountState::Pending;
    if (session_) {

        session_->post(proto::StatusEdit::bit(kServoAudioCd, audio_cd_));
    }
    disc_type_ = cdi::disc_type(geom_.toc, disc_type_);
    plan_.disc_size_bytes = proto::BlockSize{cdi::kSectorBytes}.image_bytes(geom_.toc.end);
    plan_.data_first_track = geom_.toc.tracks[svc::TrackIndex{}].type != svc::disc::TrackType::Cdda;
    probed_ = true;
    return MountState::Done;
}

MountState CdiCore::probe_boot_() {
    if (cdi::classify(kBootLba, geom_) != cdi::BlockAnswer::Track) {
        audio_cd_ = true;
        return MountState::Done;
    }
    const std::optional<proto::Lba> at = kBootLba.readable();
    if (!at) {
        audio_cd_ = true;
        return MountState::Done;
    }
    std::array<std::byte, svc::disc::kFullFrameBytes> frame;
    const svc::disc::DiscTake got = discs_->take(
        svc::disc::DiscAsk{.form = svc::disc::DiscForm::FullFrame, .lba = *at},
        frame
    );
    if (got.is_failed()) {
        audio_cd_ = true;
        return MountState::Done;
    }
    if (!got.is_bytes())
        return MountState::Pending;
    audio_cd_ = std::memcmp(frame.data(), cdi::kBootHeader.data(), cdi::kBootHeader.size()) != 0;
    return MountState::Done;
}

proto::SlotAttributes CdiCore::attributes(proto::SlotIndex) const {
    TASTY_SEAT_BODY(CdiCore);
    return proto::SlotAttributes{};
}

proto::BlockGeometry CdiCore::geometry_for(
    proto::SlotIndex slot,
    proto::Lba,
    proto::BlockGeometry wire
) {
    TASTY_SEAT_BODY(CdiCore);

    if (slot != profile().staging.disc_slot.block_slot())
        return wire;

    return proto::BlockGeometry{
        cdi::rules_after(disc_type_).block_size(),
        proto::BlockCount{cdi::hunk_window(geom_.chd_hunk_bytes())}
    };
}

}
