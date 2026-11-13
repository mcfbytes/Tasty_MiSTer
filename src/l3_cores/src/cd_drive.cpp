// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_drive.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/cd_command_round.h"
#include "cores/cd_engine.h"
#include "cores/cd_flow.h"
#include "cores/mount_answer.h"
#include "cores/mounted_path.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "svc/cue_policy.h"
#include "svc/disc_counters.h"
#include "svc/disc_mounter.h"

namespace mister::cores {

void CdDrive::service_command_edge(proto::IImageSink& sink) noexcept {
    TASTY_SEAT_BODY(CdDrive);
    if (!engine_)
        return;
    run_command_round(wire_, *engine_, slot_.geometry(), sink);
}

void CdDrive::service_tick(proto::IImageSink& sink) noexcept {
    TASTY_SEAT_BODY(CdDrive);
    if (!engine_)
        return;

    (void)slot_.refresh(engine_ref_());
    CdWireLink link{wire_, sink};
    CdDialectPorts p{slot_.geometry(), link, lamp_};
    if (auto r = engine_->on_tick(p); !r)
        wire_.count_fault();
}

MountState CdDrive::disc_mount_state() {
    TASTY_SEAT_BODY(CdDrive);
    switch (slot_.refresh(engine_ref_())) {
        case svc::DiscMountState::Pending:
            return MountState::Pending;
        case svc::DiscMountState::Mounted:
            return MountState::Done;
        case svc::DiscMountState::Failed:
        case svc::DiscMountState::Idle:
            break;
    }
    return MountState::Failed;
}

Ex<void> CdDrive::stage_disc_payload(
    const proto::LinkOp::StageDiscPayload&,
    proto::IImageSink& sink
) {
    TASTY_SEAT_BODY(CdDrive);
    if (engine_ == nullptr)
        return {};
    std::byte frame[CdEngine::kMaxDescriptorBytes]{};
    const auto written = engine_->disc_descriptor(frame, slot_.geometry());
    if (!written)
        return std::unexpected(written.error());
    if (written->empty())
        return {};

    const std::optional<IoIndex> info = cd_.io.info;
    if (!info)
        return {};
    return wire_.bracket_inline(*info, *written, sink);
}

void CdDrive::set_region(std::uint8_t region) {
    TASTY_SEAT_BODY(CdDrive);
    if (engine_)
        engine_->set_region(region != 0);
}

Ex<void> CdDrive::deferred_reset() {
    TASTY_SEAT_BODY(CdDrive);
    return reset();
}

std::optional<svc::DiscCounters> CdDrive::disc_counters() const noexcept {
    TASTY_SEAT_BODY(CdDrive);
    return slot_.counters();
}

std::optional<CdFlow> CdDrive::cd_flow() const noexcept {
    TASTY_SEAT_BODY(CdDrive);
    const CdEngine* e = engine();
    if (e == nullptr || !slot_.bound())
        return std::nullopt;
    const CdEngine::DriveView d = e->drive_view(slot_.geometry());
    const CdEngineStats c = e->stats();

    return CdFlow{
        .waits = c.flow_waits,
        .cdda_sectors = c.cdda_sectors,
        .data_sectors = c.data_sectors,
        .idle_ticks = c.idle_ticks,
        .gated_ticks = e->gated_ticks(),
        .drive_state = d.state,
        .drive_track = d.track,
        .drive_lba = d.lba,
        .drive_audio_lba = d.audio_lba,
        .substitutes = c.substitutes,
        .subcode_substitutes = c.subcode_substitutes,
        .not_resident = slot_.not_resident(),
        .drive_is_data = d.is_data,
        .busy_ticks = c.busy_ticks,
        .egress_abandons = wire_.egress_abandons()
    };
}

CdDrive::CdDrive(const CdProfile& cd, const HostServices& host, infra::OptRef<ICoreWindow> window)
    : cd_(cd),
      granted_discs_(host.discs),
      lamp_(host.lamp),
      wire_(cd, host) {

    if (!granted_discs_)
        return;

    engine_ = make_cd_engine(cd_, *granted_discs_, host.clock, window);
}

CdDrive::~CdDrive() = default;

Ex<void> CdDrive::adopt() {

    if (!granted_discs_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    slot_.adopt(granted_discs_);
    wire_.declare_resets(cd_.reset);
    return {};
}

void CdDrive::retire() noexcept {
    wire_.declare_resets(proto::ResetTerms::framework());
}

Ex<void> CdDrive::reset() {

    if (engine_)
        engine_->reset();
    return {};
}

Ex<void> CdDrive::arm_disc(const MountedPath& p) {

    if (!slot_.bound() || !engine_) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }

    const auto op = p.path.empty() ? svc::DiscOp::Unmount : svc::DiscOp::Mount;
    const svc::DiscArm arm = slot_.arm_mount(
        op,
        infra::OptRef<const svc::CuePolicy>{cd_.cue},
        p.path
    );
    if (arm == svc::DiscArm::Declined)
        return std::unexpected(mount_declined(ERR_SITE()));
    if (arm == svc::DiscArm::Failed) {
        engine_->notify_disc(false, slot_.geometry());
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)slot_.refresh(engine_ref_());
    return {};
}

Ex<void> CdDrive::eject_for_cart() {
    TASTY_SEAT_BODY(CdDrive);

    if (!slot_.bound() || !engine_ || !engine_->cart_load_ejects())
        return {};
    (void)slot_.arm_mount(
        svc::DiscOp::Unmount,
        infra::OptRef<const svc::CuePolicy>{cd_.cue},
        std::string_view{}
    );
    (void)slot_.refresh(engine_ref_());
    engine_->notify_disc(false, slot_.geometry());

    engine_->reset();
    (void)engine_->apply_pending_reset();
    return {};
}

}
