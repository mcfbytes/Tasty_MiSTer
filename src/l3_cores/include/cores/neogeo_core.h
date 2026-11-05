// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/cd_drive.h"
#include "cores/cd_profile.h"
#include "cores/cd_service_rows.h"
#include "cores/core_support.h"
#include "cores/staging_core.h"
#include "cores/stream_load_host.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores {

class NeoGeoCore final : public Core, public IStagingCore, public ICdServiceRows {
    TASTY_SEAT_RESIDENT(RT);

public:
    NeoGeoCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host)
        : Core(profile, host),
          drive_(cd, host, nullptr) {}

    [[nodiscard]] bool cd_mode() const noexcept { return cd_mode_; }

    CdDrive& drive() noexcept { return drive_; }

    [[nodiscard]] MountState mount_disc(std::string_view path) override;

    void set_region(proto::RegionCode region) override {
        TASTY_SEAT_BODY(NeoGeoCore);
        drive_.set_region(region.v);
    }

    [[nodiscard]] Ex<void> deferred_reset() override {
        TASTY_SEAT_BODY(NeoGeoCore);
        return drive_.deferred_reset();
    }

    [[nodiscard]] Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload& op) override {
        TASTY_SEAT_BODY(NeoGeoCore);
        return drive_.stage_disc_payload(op, image_sink());
    }

    void service_command_edge() noexcept override {
        TASTY_SEAT_BODY(NeoGeoCore);
        drive_.service_command_edge(image_sink());
    }

    void service_tick() noexcept override {
        TASTY_SEAT_BODY(NeoGeoCore);
        drive_.service_tick(image_sink());
    }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    void do_retire() noexcept override { drive_.retire(); }

    [[nodiscard]] Ex<void> do_reset() override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] infra::OptRef<IStagingCore> on_staging_core() noexcept override {
        return infra::OptRef<IStagingCore>{*this};
    }

    [[nodiscard]] infra::OptRef<ICdServiceRows> on_cd_service_rows() noexcept override {
        return cd_mode_ ? infra::OptRef<ICdServiceRows>{*this} : infra::OptRef<ICdServiceRows>{};
    }

    [[nodiscard]] infra::OptRef<const ICdDiagnostics> on_cd_diagnostics() const noexcept override {
        return cd_mode_ ? infra::OptRef<const ICdDiagnostics>{drive_}
                        : infra::OptRef<const ICdDiagnostics>{};
    }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    void on_set_pending_file_ext(std::string_view ext) noexcept override {
        stream_.set_pending_ext(ext);
    }

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override {
        return stream_.bytes();
    }

    [[nodiscard]] svc::Crc32 on_last_tx_crc() const noexcept override { return stream_.crc(); }

    StreamLoadHost stream_;
    bool cd_mode_ = false;
    CdDrive drive_;
};

}
