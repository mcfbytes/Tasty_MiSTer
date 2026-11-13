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
#include "cores/toc_entry.h"
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
          cue_(cue) {}

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return geom_; }

    [[nodiscard]] bool audio_cd() const noexcept { return audio_cd_; }

    [[nodiscard]] DiscType disc_type() const noexcept { return disc_type_; }

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

    void clear_mount_() noexcept;
    svc::DiscMountState refresh_disc_() noexcept;

    [[nodiscard]] MountState probe_boot_();

    [[nodiscard]] MountState finish_mount_();

    const svc::CuePolicy& cue_;

    infra::OptRef<svc::DiscReadService> discs_{};
    infra::OptRef<proto::CoreSession> session_{};
    svc::DiscMountState mount_seen_ = svc::DiscMountState::Idle;
    svc::DiscGeometry geom_{};

    FixedStr<1024, StrFit::Reject> image_path_{};

    bool probed_ = false;
    bool audio_cd_ = false;

    DiscType disc_type_ = DiscType::CdRom;
    StagePlan plan_{};
};

}
