// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/cd_diagnostics.h"
#include "cores/cd_disc_slot.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/cd_wire_port.h"
#include "cores/core_support.h"
#include "cores/staging_core.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/image_sink.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

struct CdFlow;
struct MountedPath;

class CdDrive final : public ICdDiagnostics {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdDrive(const CdProfile& cd, const HostServices& host, infra::OptRef<ICoreWindow> window);
    ~CdDrive() override;
    CdDrive(const CdDrive&) = delete;
    CdDrive& operator=(const CdDrive&) = delete;

    [[nodiscard]] Ex<void> adopt();

    void retire() noexcept;

    [[nodiscard]] Ex<void> reset();

    [[nodiscard]] Ex<void> arm_disc(const MountedPath& p);

    [[nodiscard]] Ex<void> eject_for_cart();

    const CdProfile& cd_profile() const noexcept { return cd_; }

    CdEngine* engine() noexcept { return engine_.get(); }

    [[nodiscard]] const CdEngine* engine() const noexcept { return engine_.get(); }

    [[nodiscard]] infra::OptRef<svc::DiscReadService> discs() const noexcept {
        return slot_.discs();
    }

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return slot_.geometry(); }

    std::uint32_t wire_faults() const noexcept { return wire_.wire_faults(); }

    [[nodiscard]] bool cdda_capture_armed() const noexcept { return wire_.cdda_capture_armed(); }

    void service_command_edge(proto::IImageSink& sink) noexcept;
    void service_tick(proto::IImageSink& sink) noexcept;

    [[nodiscard]] std::optional<svc::DiscCounters> disc_counters() const noexcept override;
    [[nodiscard]] std::optional<CdFlow> cd_flow() const noexcept override;

    [[nodiscard]] MountState disc_mount_state();
    [[nodiscard]] Ex<void> stage_disc_payload(
        const proto::LinkOp::StageDiscPayload&,
        proto::IImageSink& sink
    );
    void set_region(std::uint8_t region);
    [[nodiscard]] Ex<void> deferred_reset();

private:
    [[nodiscard]] infra::OptRef<CdEngine> engine_ref_() noexcept {
        return engine_ ? infra::OptRef<CdEngine>{*engine_} : infra::OptRef<CdEngine>{};
    }

    const CdProfile& cd_;
    infra::OptRef<svc::DiscReadService> granted_discs_{};
    infra::OptRef<hal::DiskLamp> lamp_{};
    CdWirePort wire_;
    CdDiscSlot slot_;
    std::unique_ptr<CdEngine> engine_;
};

}
