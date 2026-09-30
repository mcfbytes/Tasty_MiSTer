// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/cd_diagnostics.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_flow_query.h"
#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_service_rows.h"
#include "cores/cd_transport.h"
#include "cores/core_support.h"
#include "cores/staging_core.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

struct CdFlow;
struct MountedPath;

class CdDrive final : public ICdTransport,
                      public ICdFlowQuery,
                      public ICdSectorEgress,
                      public IStagingCore,
                      public ICdServiceRows,
                      public ICdDiagnostics {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdDrive(Core& owner, const CdProfile& cd, const HostServices& host);
    ~CdDrive() override;
    CdDrive(const CdDrive&) = delete;
    CdDrive& operator=(const CdDrive&) = delete;

    [[nodiscard]] Ex<void> init(ICoreWindow* window);

    [[nodiscard]] Ex<void> reset();

    [[nodiscard]] Ex<void> arm_disc(const MountedPath& p);

    [[nodiscard]] Ex<void> eject_for_cart();

    const CdProfile& cd_profile() const noexcept { return *cd_; }
    CdEngine* engine() noexcept { return engine_.get(); }
    [[nodiscard]] const CdEngine* engine() const noexcept { return engine_.get(); }
    svc::DiscReadService* discs() noexcept { return discs_; }
    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return geom_; }
    ICdTransport& transport() noexcept { return *this; }
    std::uint32_t wire_faults() const noexcept { return wire_faults_; }
    [[nodiscard]] bool cdda_capture_armed() const noexcept { return cdda_cap_ != nullptr; }

    void service_command_edge() noexcept override;
    void service_tick() noexcept override;

    [[nodiscard]] std::optional<svc::DiscCounters> disc_counters() const noexcept override;
    [[nodiscard]] std::optional<CdFlow> cd_flow() const noexcept override;

    [[nodiscard]] proto::IImageSink& image_sink() noexcept override;

private:
    [[nodiscard]] Ex<void> bracket_inline_(IoIndex io_index, std::span<const std::byte> payload);
    [[nodiscard]] std::uint32_t egress_abandons_() const noexcept;
    svc::DiscMountState refresh_disc_() noexcept;

    [[nodiscard]] Ex<void> send_data(IoIndex io_index, std::span<const std::byte> payload) override;
    [[nodiscard]] Ex<void> send_status(std::uint64_t bcd_frame) override;
    bool can_send_data(svc::TrackType type) override;
    [[nodiscard]] bool egress_idle() const noexcept override;
    [[nodiscard]] Ex<void> submit_sector(const Act& act) override;

    [[nodiscard]] MountState mount_disc(std::string_view path) override;
    [[nodiscard]] Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload&) override;
    void set_region(std::uint8_t region) override;
    [[nodiscard]] Ex<void> deferred_reset() override;

    struct CddaCapture;
    std::unique_ptr<CddaCapture> cdda_cap_;

    Core* owner_;
    const CdProfile* cd_;
    hal::ISpiTransport* link_;
    const os::IClock* clock_;
    svc::DiscReadService* granted_discs_;
    proto::SpiFioQueue* fio_queue_;
    std::uint32_t wire_faults_ = 0;
    svc::DiscGeometry geom_{};
    svc::DiscCounters counters_{};

    svc::DiscReadService* discs_ = nullptr;
    std::optional<svc::DiscReadService> own_discs_;
    svc::DiscMountState mount_seen_ = svc::DiscMountState::Idle;

    std::unique_ptr<CdEngine> engine_;
};

}  // namespace mister::cores
