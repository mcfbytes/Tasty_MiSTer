// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cores/staging_core.h"
#include "cores/cd_core.h"
#include "cores/core_support.h"
#include "cores/sector_kick.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "cores/cd_diagnostics.h"
#include "proto/block_geometry_hook.h"
#include "svc/disc_read_service.h"
#include "svc/toc_frame.h"

namespace mister::cores {

class PsxCore final : public Core,
                      public IStagingCore,
                      public ISectorKick,
                      public ICdDiagnostics,
                      public proto::IResidentImageSource,
                      public proto::IBlockGeometry {
    TASTY_SEAT_RESIDENT(RT);

public:
    PsxCore(const CoreProfile& profile, const svc::CuePolicy& cue, const HostServices& host)
        : Core(profile, host), cue_(&cue) {}

    [[nodiscard]] std::optional<svc::DiscCounters> disc_counters() const noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        if (discs_ == nullptr) return std::nullopt;
        return counters_;
    }
    [[nodiscard]] std::optional<CdFlow> cd_flow() const noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        return std::nullopt;
    }

    svc::DiscReadService* discs() noexcept { return discs_; }
    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return geom_; }

    [[nodiscard]] std::string_view game_id() const noexcept { return game_id_.view(); }
    [[nodiscard]] svc::DiscRegion region() const noexcept { return region_; }
    [[nodiscard]] std::uint32_t kicks() const noexcept { return kicks_; }
    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return wire_faults_; }
    [[nodiscard]] std::uint32_t filler_sectors() const noexcept { return filler_sectors_; }
    [[nodiscard]] std::uint32_t pregap_zero_sectors() const noexcept {
        return pregap_zero_sectors_;
    }

    [[nodiscard]] MountState mount_disc(std::string_view path) override;
    void set_region(std::uint8_t region) override;
    [[nodiscard]] Ex<void> deferred_reset() override {
        TASTY_SEAT_BODY(PsxCore);
        return {};
    }
    [[nodiscard]] proto::IImageSink& image_sink() noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        return Core::image_sink();
    }
    StagePlan stage_plan() noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        return plan_;
    }
    [[nodiscard]] Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) override;

    void service_kick() noexcept override;

    proto::SlotAttributes attributes(proto::SlotIndex slot) const override;
    [[nodiscard]] Ex<std::size_t> read_at(proto::SlotIndex slot, std::uint64_t offset,
                                          std::span<std::uint8_t> dst) override;
    [[nodiscard]] Ex<std::size_t> write_at(proto::SlotIndex slot, std::uint64_t offset,
                                           std::span<const std::uint8_t> src) override;

    proto::BlockGeometry geometry_for(proto::SlotIndex slot, proto::Lba lba,
                                      proto::BlockGeometry wire) override;

private:
    [[nodiscard]] Ex<void> do_init(proto::CoreSession& s) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    [[nodiscard]] IStagingCore* on_staging_core() noexcept override { return this; }
    [[nodiscard]] ISectorKick* on_sector_kick() noexcept override { return this; }
    [[nodiscard]] proto::IResidentImageSource* on_block_source() noexcept override { return this; }
    [[nodiscard]] proto::IBlockGeometry* on_block_geometry() noexcept override { return this; }

    [[nodiscard]] const ICdDiagnostics* on_cd_diagnostics() const noexcept override { return this; }

    [[nodiscard]] bool serve_sector_(std::int64_t lba, std::span<std::uint8_t> out) noexcept;

    void warm_window_(std::int64_t lba0, std::size_t sectors) noexcept;

    enum class Answer : std::uint8_t { Zeros, Filler, Pregap, Engine };
    [[nodiscard]] Answer classify_(std::int64_t lba) const noexcept;

    [[nodiscard]] bool scan_disc_identity_();
    svc::DiscMountState refresh_disc_() noexcept;
    [[nodiscard]] Ex<void> finish_mount_();

    const svc::CuePolicy* cue_;

    svc::DiscReadService* discs_ = nullptr;
    svc::DiscCounters counters_{};
    svc::DiscReadService::CountCell::Reader counts_{};
    std::optional<svc::DiscReadService> own_discs_;
    svc::DiscMountState mount_seen_ = svc::DiscMountState::Idle;

    std::int64_t scan_lba_ = 0;
    bool scan_done_ = false;
    svc::DiscRegion scan_prefix_region_ = svc::DiscRegion::Unknown;

    svc::DiscGeometry geom_{};

    std::string image_path_;

    StagePlan plan_{};
    FixedStr<11, StrFit::Clip> game_id_{};
    svc::DiscRegion region_ = svc::DiscRegion::Unknown;

    std::uint32_t kicks_ = 0;
    std::uint32_t wire_faults_ = 0;
    std::uint32_t filler_sectors_ = 0;
    std::uint32_t pregap_zero_sectors_ = 0;
};

}  // namespace mister::cores
