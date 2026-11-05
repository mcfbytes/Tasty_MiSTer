// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/cdi_block.h"
#include "cores/core_support.h"
#include "cores/staging_core.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/block_server.h"
#include "proto/core_session.h"
#include "svc/cue_policy.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

class CdiCore final : public Core, public IStagingCore, public proto::IBlockServer {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdiCore(const CoreProfile& profile, const svc::CuePolicy& cue, const HostServices& host)
        : Core(profile, host),
          cue_(&cue) {}

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return geom_; }

    [[nodiscard]] bool audio_cd() const noexcept { return audio_cd_; }

    [[nodiscard]] DiscType disc_type() const noexcept { return disc_type_; }

    [[nodiscard]] std::size_t toc_entries() const noexcept { return toc_count_; }

    [[nodiscard]] std::uint32_t declines() const noexcept { return declines_; }

    [[nodiscard]] std::uint32_t failed_reads() const noexcept { return failed_reads_; }

    [[nodiscard]] MountState mount_disc(std::string_view path) override;

    void set_region(proto::RegionCode) override { TASTY_SEAT_BODY(CdiCore); }

    [[nodiscard]] Ex<void> deferred_reset() override {
        TASTY_SEAT_BODY(CdiCore);
        return {};
    }

    StagePlan stage_plan() noexcept override {
        TASTY_SEAT_BODY(CdiCore);
        return plan_;
    }

    proto::SlotAttributes attributes(proto::SlotIndex slot) const override;
    [[nodiscard]] Ex<std::size_t> read_at(
        proto::SlotIndex slot,
        std::uint64_t offset,
        std::span<std::uint8_t> dst
    ) override;
    [[nodiscard]] Ex<std::size_t> write_at(
        proto::SlotIndex slot,
        std::uint64_t offset,
        std::span<const std::uint8_t> src
    ) override;

    proto::BlockGeometry geometry_for(
        proto::SlotIndex slot,
        proto::Lba lba,
        proto::BlockGeometry wire
    ) override;

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] infra::OptRef<IStagingCore> on_staging_core() noexcept override {
        return infra::OptRef<IStagingCore>{*this};
    }

    [[nodiscard]] infra::OptRef<proto::IBlockServer> on_block_server() noexcept override {
        return infra::OptRef<proto::IBlockServer>{*this};
    }

    enum class Answer : std::uint8_t { TocArea, Track, LeadOut };
    [[nodiscard]] Answer classify_(std::int32_t lba) const noexcept;

    [[nodiscard]] bool serve_block_(
        std::int32_t lba,
        std::span<std::uint8_t, cdi::kBlockBytes> out
    ) noexcept;
    void warm_(std::int32_t lba0, std::size_t blocks) noexcept;
    svc::DiscMountState refresh_disc_() noexcept;

    [[nodiscard]] MountState finish_mount_();

    const svc::CuePolicy* cue_;

    infra::OptRef<svc::DiscReadService> discs_{};
    infra::OptRef<proto::CoreSession> session_{};
    svc::DiscMountState mount_seen_ = svc::DiscMountState::Idle;
    svc::DiscGeometry geom_{};

    FixedStr<1024, StrFit::Reject> image_path_{};

    bool probed_ = false;
    bool audio_cd_ = false;

    DiscType disc_type_ = DiscType::CdRom;
    std::array<cdi::TocEntry, cdi::kTocEntries> toc_{};
    std::size_t toc_count_ = 0;
    StagePlan plan_{};
    std::uint32_t declines_ = 0;
    std::uint32_t failed_reads_ = 0;
};

}
