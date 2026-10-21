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
#include "cores/cd_flow.h"
#include "cores/cd_flow_query.h"
#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_service_rows.h"
#include "cores/cd_transport.h"
#include "cores/core_support.h"
#include "cores/stream_load.h"
#include "cores/stream_tx_record.h"
#include "cores/file_tx.h"
#include "cores/nvram_store.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "svc/disc_read_service.h"
#include "infra/seat.h"

namespace mister::cores {

class CdCore : public Core, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host);

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
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    void do_retire() noexcept override { drive_.retire(); }

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

    void on_set_pending_file_ext(std::string_view ext) noexcept override { tx_.set_ext(ext); }

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return tx_.bytes(); }

    [[nodiscard]] std::uint32_t on_last_tx_crc() const noexcept override { return tx_.crc(); }

    StreamTxRecord tx_;
    CdDrive drive_;
};

}
