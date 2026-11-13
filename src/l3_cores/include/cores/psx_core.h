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
#include "cores/psx_sector_map.h"
#include "cores/sector_kick.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "cores/cd_diagnostics.h"
#include "proto/block_server.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

struct PsxGameId {
    TASTY_SEAT_EXEMPT(component);
    static constexpr std::size_t kChars = 11;
    FixedStr<kChars, StrFit::Clip> text{};

    [[nodiscard]] std::string_view view() const noexcept { return text.view(); }

    [[nodiscard]] bool empty() const noexcept { return text.empty(); }

    void clear() noexcept { text.clear(); }

    void assign(std::span<const std::uint8_t> hit, std::size_t raw_len) noexcept {
        text.clear();
        const std::size_t n = raw_len > kChars ? kChars : raw_len;
        const bool full = raw_len == kChars;
        for (std::size_t i = 0; i < n; ++i) {

            if (full && i == 8 && hit[i] == '.')
                continue;
            const char c = full && i == 4 && hit[i] == '_' ? '-' : static_cast<char>(hit[i]);
            (void)text.append(std::string_view{&c, 1});
        }
    }
};

class PsxCore final : public Core,
                      public IStagingCore,
                      public ISectorKick,
                      public ICdDiagnostics,
                      public proto::IBlockServer {
    TASTY_SEAT_RESIDENT(RT);

public:
    PsxCore(const CoreProfile& profile, const svc::CuePolicy& cue, const HostServices& host)
        : Core(profile, host),
          cue_(cue) {}

    [[nodiscard]] std::optional<svc::DiscCounters> disc_counters() const noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        if (!discs_)
            return std::nullopt;
        return counters_;
    }

    [[nodiscard]] std::optional<CdFlow> cd_flow() const noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        return std::nullopt;
    }

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return geom_; }

    [[nodiscard]] std::string_view game_id() const noexcept { return geom_.identity.id.view(); }

    [[nodiscard]] svc::DiscRegion region() const noexcept { return geom_.identity.region; }

    [[nodiscard]] std::uint32_t kicks() const noexcept { return kicks_; }

    [[nodiscard]] std::uint32_t held_kicks() const noexcept { return held_kicks_; }

    [[nodiscard]] std::uint32_t wire_faults() const noexcept { return wire_faults_; }

    [[nodiscard]] MountState mount_disc(std::string_view path) override;
    void set_region(proto::RegionCode region) override;

    [[nodiscard]] Ex<void> deferred_reset() override {
        TASTY_SEAT_BODY(PsxCore);
        return {};
    }

    StagePlan stage_plan() noexcept override {
        TASTY_SEAT_BODY(PsxCore);
        return plan_;
    }

    [[nodiscard]] Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) override;

    void service_kick() noexcept override;

    proto::SlotAttributes attributes(proto::SlotIndex slot) const override;

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

    [[nodiscard]] infra::OptRef<ISectorKick> on_sector_kick() noexcept override {
        return infra::OptRef<ISectorKick>{*this};
    }

    [[nodiscard]] infra::OptRef<proto::IBlockServer> on_block_server() noexcept override {
        return infra::OptRef<proto::IBlockServer>{*this};
    }

    [[nodiscard]] infra::OptRef<const ICdDiagnostics> on_cd_diagnostics() const noexcept override {
        return infra::OptRef<const ICdDiagnostics>{*this};
    }

    void clear_mount_() noexcept;
    svc::DiscMountState refresh_disc_() noexcept;

    [[nodiscard]] bool finish_mount_();

    [[nodiscard]] bool copy_in_flight_() const noexcept;

    infra::OptRef<const svc::CuePolicy> cue_;

    infra::OptRef<svc::DiscReadService> discs_{};
    svc::DiscCounters counters_{};
    svc::DiscMountState mount_seen_ = svc::DiscMountState::Idle;

    svc::DiscGeometry geom_{};

    FixedStr<1024, StrFit::Reject> image_path_{};

    StagePlan plan_{};

    std::uint32_t kicks_ = 0;
    std::uint32_t held_kicks_ = 0;
    std::uint32_t wire_faults_ = 0;
};

}
