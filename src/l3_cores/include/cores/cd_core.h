// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/cd_drive.h"
#include "cores/cd_diagnostics.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_flow_query.h"
#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_service_rows.h"
#include "cores/cd_transport.h"
#include "cores/core_support.h"
#include "cores/stream_load.h"
#include "cores/file_tx.h"
#include "cores/nvram_store.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "svc/disc_read_service.h"
#include "infra/seat.h"

namespace mister::cores {

struct CdFlow {
    std::uint32_t waits = 0;
    std::uint32_t cdda_sectors = 0;

    std::uint32_t data_sectors = 0;
    std::uint32_t idle_ticks = 0;

    std::uint32_t gated_ticks = 0;
    std::uint8_t drive_state = 0;
    std::uint32_t drive_track = 0;
    std::int32_t drive_lba = 0;

    std::int32_t drive_audio_lba = 0;

    std::uint32_t substitutes = 0;
    std::uint32_t subcode_substitutes = 0;
    std::uint32_t not_resident = 0;
    bool drive_is_data = true;

    std::uint32_t busy_ticks = 0;
    std::uint32_t egress_abandons = 0;
};

class CdCore : public Core, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host)
        : Core(profile, host), drive_(*this, cd, host) {}

    const CdProfile& cd_profile() const noexcept { return drive_.cd_profile(); }

    CdDrive& drive() noexcept { return drive_; }

    CdEngine* engine() noexcept { return drive_.engine(); }
    [[nodiscard]] const CdEngine* engine() const noexcept { return drive_.engine(); }
    svc::DiscReadService* discs() noexcept { return drive_.discs(); }

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return drive_.geometry(); }
    ICdTransport& transport() noexcept { return drive_.transport(); }
    void service_command_edge() noexcept { drive_.service_command_edge(); }
    void service_tick() noexcept { drive_.service_tick(); }
    std::uint32_t wire_faults() const noexcept { return drive_.wire_faults(); }
    [[nodiscard]] bool cdda_capture_armed() const noexcept { return drive_.cdda_capture_armed(); }

private:
    [[nodiscard]] Ex<void> do_init(proto::CoreSession& s) override;
    [[nodiscard]] Ex<void> do_reset() override { return drive_.reset(); }
    [[nodiscard]] IStagingCore* on_staging_core() noexcept override { return &drive_; }
    [[nodiscard]] ICdServiceRows* on_cd_service_rows() noexcept override { return &drive_; }
    [[nodiscard]] const ICdDiagnostics* on_cd_diagnostics() const noexcept override {
        return &drive_;
    }
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;
    [[nodiscard]] IStreamLoad* on_stream_load() noexcept override { return this; }
    void on_set_pending_file_ext(std::string_view ext) noexcept override {
        (void)tx_ext_.assign(ext);
    }
    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return tx_bytes_; }
    [[nodiscard]] std::uint32_t on_last_tx_crc() const noexcept override { return tx_crc_; }

    FixedStr<8, StrFit::Clip> tx_ext_{};
    std::uint64_t tx_bytes_ = 0;
    std::uint32_t tx_crc_ = 0;
    CdDrive drive_;
};

}  // namespace mister::cores
