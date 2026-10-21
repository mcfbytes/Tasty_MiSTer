// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_drive.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/cd_engine.h"
#include "cores/cd_flow.h"
#include "cores/mounted_path.h"
#include "infra/error.h"
#include "svc/disc_engine.h"

namespace mister::cores {

Ex<void> CdDrive::send_data(IoIndex io_index, std::span<const std::byte> payload) {
    TASTY_SEAT_BODY(CdDrive);
    return wire_.send_data(io_index, payload, owner_->image_sink());
}

bool CdDrive::egress_idle() const noexcept {
    TASTY_SEAT_BODY(CdDrive);
    return wire_.egress_idle();
}

Ex<void> CdDrive::submit_sector(const Act& act) {
    TASTY_SEAT_BODY(CdDrive);
    return wire_.submit_sector(act);
}

bool CdDrive::can_send_data(std::uint16_t selector) {
    TASTY_SEAT_BODY(CdDrive);
    return wire_.can_send_data(selector);
}

Ex<void> CdDrive::send_status(std::uint64_t bcd_frame) {
    TASTY_SEAT_BODY(CdDrive);
    if (!engine_) {

        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }
    std::uint16_t words[CdEngine::kMaxStatusWords] = {};
    const std::size_t n = engine_->status_words(bcd_frame, std::span{words});
    return wire_.send_status(std::span<const std::uint16_t>(words, n));
}

void CdDrive::service_command_edge() noexcept {
    TASTY_SEAT_BODY(CdDrive);
    if (!engine_)
        return;
    wire_.service_command_edge(*engine_);
}

void CdDrive::service_tick() noexcept {
    TASTY_SEAT_BODY(CdDrive);
    if (!engine_)
        return;

    (void)slot_.refresh(engine_ref_());
    if (auto r = engine_->on_tick(); !r)
        wire_.count_fault();
}

MountState CdDrive::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(CdDrive);

    const auto m = owner_->mount(
        owner_->profile().staging.disc_slot,
        MountedPath{proto::PathId{}, path, proto::FileSize{}}
    );
    if (!m)
        return MountState::Failed;
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

Ex<void> CdDrive::stage_disc_payload(const proto::LinkOp::StageDiscPayload&) {
    TASTY_SEAT_BODY(CdDrive);
    if (engine_ == nullptr)
        return {};
    std::byte frame[CdEngine::kMaxDescriptorBytes]{};
    auto n = engine_->disc_descriptor(frame);
    if (!n)
        return std::unexpected(n.error());
    if (*n == 0)
        return {};

    return wire_
        .bracket_inline(cd_->io.info, std::span<const std::byte>(frame, *n), owner_->image_sink());
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

proto::IImageSink& CdDrive::image_sink() noexcept {
    TASTY_SEAT_BODY(CdDrive);
    return owner_->image_sink();
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
    const CdEngine::DriveView d = e->drive_view();
    const CdEngineStats c = e->stats();
    return CdFlow{
        c.flow_waits,
        c.cdda_sectors,
        c.data_sectors,
        c.idle_ticks,
        e->gated_ticks(),
        d.state,
        d.track,
        d.lba,
        d.audio_lba,
        c.substitutes,
        c.subcode_substitutes,
        slot_.not_resident(),
        d.is_data,
        c.busy_ticks,
        wire_.egress_abandons()
    };
}

CdDrive::CdDrive(Core& owner, const CdProfile& cd, const HostServices& host, ICoreWindow* window)
    : owner_(&owner),
      cd_(&cd),
      granted_discs_(host.discs),
      wire_(cd, host) {

    if (!granted_discs_)
        return;

    ICdSectorEgress* const egress = wire_.queued() ? this : nullptr;

    if (auto e = make_cd_engine(
            *cd_,
            *granted_discs_,
            slot_.geometry(),
            transport(),
            host.clock,
            this,
            egress,
            window
        )) {
        engine_ = std::move(*e);
    }
}

CdDrive::~CdDrive() = default;

Ex<void> CdDrive::adopt() {

    if (!granted_discs_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    if (!engine_) {
        return std::unexpected(
            Error{Errc::core_load, ERR_SITE(), static_cast<std::uint32_t>(cd_->dialect)}
        );
    }
    slot_.adopt(granted_discs_);
    wire_.declare_resets(cd_->reset);
    return {};
}

void CdDrive::retire() noexcept {
    wire_.declare_resets(proto::ResetTerms::framework());
}

Ex<void> CdDrive::reset() {

    if (engine_)
        return engine_->reset();
    return {};
}

Ex<void> CdDrive::arm_disc(const MountedPath& p) {

    if (!slot_.bound() || !engine_) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
    }

    const auto op = p.display.empty() ? svc::DiscOp::Unmount : svc::DiscOp::Mount;
    const svc::DiscMountState st = slot_.arm_mount(op, &cd_->cue, p.display);
    if (st == svc::DiscMountState::Failed) {
        (void)engine_->notify_disc(false);
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    }
    (void)slot_.refresh(engine_ref_());
    return {};
}

Ex<void> CdDrive::eject_for_cart() {
    TASTY_SEAT_BODY(CdDrive);

    if (!slot_.bound() || !engine_ || !engine_->cart_load_ejects())
        return {};
    (void)slot_.arm_mount(svc::DiscOp::Unmount, &cd_->cue, std::string_view{});
    (void)slot_.refresh(engine_ref_());
    if (auto n = engine_->notify_disc(false); !n)
        return std::unexpected(n.error());

    if (auto r = engine_->reset(); !r)
        return std::unexpected(r.error());
    (void)engine_->apply_pending_reset();
    return {};
}

}
