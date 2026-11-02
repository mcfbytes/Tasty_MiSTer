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
#include "cores/cd_profile.h"
#include "cores/cd_service_rows.h"
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

class CdCore : public Core, public IStreamLoad, public IStagingCore, public ICdServiceRows {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host);

    const CdProfile& cd_profile() const noexcept { return drive_.cd_profile(); }

    CdDrive& drive() noexcept { return drive_; }

    CdEngine* engine() noexcept { return drive_.engine(); }

    [[nodiscard]] const CdEngine* engine() const noexcept { return drive_.engine(); }

    [[nodiscard]] infra::OptRef<svc::DiscReadService> discs() const noexcept {
        return drive_.discs();
    }

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return drive_.geometry(); }

    [[nodiscard]] MountState mount_disc(std::string_view path) override;

    void set_region(std::uint8_t region) override {
        TASTY_SEAT_BODY(CdCore);
        drive_.set_region(region);
    }

    [[nodiscard]] Ex<void> deferred_reset() override {
        TASTY_SEAT_BODY(CdCore);
        return drive_.deferred_reset();
    }

    [[nodiscard]] Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) override {
        TASTY_SEAT_BODY(CdCore);
        return drive_.stage_disc_payload(op, image_sink());
    }

    void service_command_edge() noexcept override {
        TASTY_SEAT_BODY(CdCore);
        drive_.service_command_edge(image_sink());
    }

    void service_tick() noexcept override {
        TASTY_SEAT_BODY(CdCore);
        drive_.service_tick(image_sink());
    }

    std::uint32_t wire_faults() const noexcept { return drive_.wire_faults(); }

    [[nodiscard]] bool cdda_capture_armed() const noexcept { return drive_.cdda_capture_armed(); }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    void do_retire() noexcept override { drive_.retire(); }

    [[nodiscard]] Ex<void> do_reset() override { return drive_.reset(); }

    [[nodiscard]] IStagingCore* on_staging_core() noexcept override { return this; }

    [[nodiscard]] ICdServiceRows* on_cd_service_rows() noexcept override { return this; }

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
