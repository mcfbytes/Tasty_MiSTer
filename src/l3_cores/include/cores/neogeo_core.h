// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/cd_drive.h"
#include "cores/cd_profile.h"
#include "cores/core_support.h"
#include "cores/stream_load.h"
#include "cores/stream_tx_record.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::cores {

class NeoGeoCore final : public Core, public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    NeoGeoCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host)
        : Core(profile, host),
          drive_(*this, cd, host, nullptr) {}

    [[nodiscard]] bool cd_mode() const noexcept { return cd_mode_; }

    CdDrive& drive() noexcept { return drive_; }

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    void do_retire() noexcept override { drive_.retire(); }

    [[nodiscard]] Ex<void> do_reset() override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] IStagingCore* on_staging_core() noexcept override { return &drive_; }

    [[nodiscard]] ICdServiceRows* on_cd_service_rows() noexcept override {
        return cd_mode_ ? &drive_ : nullptr;
    }

    [[nodiscard]] const ICdDiagnostics* on_cd_diagnostics() const noexcept override {
        return cd_mode_ ? &drive_ : nullptr;
    }

    [[nodiscard]] IStreamLoad* on_stream_load() noexcept override { return this; }

    void on_set_pending_file_ext(std::string_view ext) noexcept override { tx_.set_ext(ext); }

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return tx_.bytes(); }

    [[nodiscard]] std::uint32_t on_last_tx_crc() const noexcept override { return tx_.crc(); }

    StreamTxRecord tx_;
    bool cd_mode_ = false;
    CdDrive drive_;
};

}
