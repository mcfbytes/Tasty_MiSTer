// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_core.h"

#include <cstring>

#include "cores/cdi_disc_rules.h"
#include "cores/core_adopt.h"
#include "cores/mount_answer.h"
#include "cores/mounted_path.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "svc/track.h"
#include "proto/block_geometry.h"

namespace mister::cores {
namespace {

constexpr HeadLba kBootLba{166};
constexpr proto::StatusBit kServoAudioCd{12};

}

Ex<void> CdiCore::do_adopt(const CoreAdopt& a) {
    if (!host().discs)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    session_ = infra::OptRef<proto::CoreSession>{a.session};
    discs_ = host().discs;
    geom_ = discs_->geometry();
    return {};
}

MountState CdiCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(CdiCore);
    const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
    if (!m)
        return mount_answer_of(m.error());
    if (path.empty())
        return MountState::Done;
    switch (refresh_disc_()) {
        case svc::DiscMountState::Pending:
            return MountState::Pending;
        case svc::DiscMountState::Mounted:
            return finish_mount_();
        case svc::DiscMountState::Failed:
        case svc::DiscMountState::Idle:
            break;
    }
    return MountState::Failed;
}

Ex<void> CdiCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot != profile().staging.disc_slot)
        return {};
    if (!discs_)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});

    if (mount_seen_ == svc::DiscMountState::Mounted && image_path_.view() == p.path)
        return {};
    if (p.path.empty()) {
        if (discs_->arm_mount(
                svc::DiscOp::Unmount,
                infra::OptRef<const svc::CuePolicy>{cue_},
                std::string_view{}
            )
            == svc::DiscArm::Declined) {
            return std::unexpected(mount_declined(ERR_SITE()));
        }
        clear_mount_();
        (void)refresh_disc_();
        return {};
    }

    if (!infra::iends_with(p.path, ".cue") && !infra::iends_with(p.path, ".chd")) {
        clear_mount_();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const svc::DiscArm arm = discs_->arm_mount(
        svc::DiscOp::Mount,
        infra::OptRef<const svc::CuePolicy>{cue_},
        p.path,
        infra::OptRef<const svc::IDiscRules>{cdi::rules_after(disc_type_)}
    );
    if (arm == svc::DiscArm::Declined)
        return std::unexpected(mount_declined(ERR_SITE()));
    clear_mount_();
    (void)refresh_disc_();
    if (arm == svc::DiscArm::Failed) {
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

svc::DiscMountState CdiCore::refresh_disc_() noexcept {
    if (!discs_)
        return svc::DiscMountState::Failed;
    const svc::DiscMountState st = discs_->mount_state();
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
    plan_.disc_size_bytes = proto::FileSize{
        static_cast<std::uint64_t>(geom_.toc.end.v) * cdi::kSectorBytes
    };
    plan_.data_first_track = geom_.toc.at(svc::TrackIndex{}).type != svc::TrackType::Cdda;
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
    std::array<std::byte, svc::kFullFrameBytes> frame;
    const svc::DiscTake got = discs_->take(
        svc::DiscAsk{.form = svc::DiscForm::FullFrame, .lba = *at},
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
