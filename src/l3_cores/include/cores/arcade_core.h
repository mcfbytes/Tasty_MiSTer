// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cores/arcade_facts.h"
#include "cores/core_grant.h"
#include "cores/core_support.h"
#include "cores/dip_switches.h"
#include "cores/mra_doc.h"
#include "cores/save_upload.h"
#include "cores/stream_load_host.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::cores {

inline constexpr CoreProfile kArcadeProfile{
    .kind = CoreKind::Arcade,
    .name = "",
    .rbf = "",
    .services = {},
    .slots = {},
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},
    .undeclared_role = SlotRole::Save,
};

class ArcadeCore final : public Core, public IDipSwitches, public ISaveUpload {
    TASTY_SEAT_RESIDENT(RT);

public:
    ArcadeCore(const CoreProfile& profile, const HostServices& host, ArcadeFacts facts);

    [[nodiscard]] std::size_t dip_count() const noexcept override;
    [[nodiscard]] DipRowView dip_row(proto::DipRowIndex row) const noexcept override;
    [[nodiscard]] std::string_view dip_choice(
        proto::DipRowIndex row,
        proto::DipChoice choice
    ) const noexcept override;
    [[nodiscard]] Ex<void> set_dip(proto::DipRowIndex row, proto::DipChoice choice) override;

    [[nodiscard]] std::string_view dip_file_name() const noexcept override {
        TASTY_SEAT_BODY(ArcadeCore);
        return dip_name_;
    }

    [[nodiscard]] std::array<std::uint8_t, 8> dip_bytes() const noexcept override;
    [[nodiscard]] bool dips_dirty() const noexcept override;

    void mark_dips_saved() noexcept override {
        TASTY_SEAT_BODY(ArcadeCore);
        dip_saved_ = dip_cur_;
    }

    [[nodiscard]] std::string_view save_file_name() const noexcept override {
        TASTY_SEAT_BODY(ArcadeCore);
        return nvm_name_;
    }

    [[nodiscard]] SaveImage save_image() const noexcept override;

    [[nodiscard]] std::string_view nvram_file_name() const noexcept { return nvm_name_; }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt&) override { return {}; }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{stream_};
    }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    std::uint64_t on_last_tx_bytes() const noexcept override;
    svc::Crc32 on_last_tx_crc() const noexcept override;

    [[nodiscard]] infra::OptRef<IDipSwitches> on_dip_switches() noexcept override {
        return infra::OptRef<IDipSwitches>{*this};
    }

    [[nodiscard]] infra::OptRef<ISaveUpload> on_save_upload() noexcept override {
        return nvram_ ? infra::OptRef<ISaveUpload>{*this} : infra::OptRef<ISaveUpload>{};
    }

    [[nodiscard]] Ex<void> on_osd_closed() override { return {}; }

    [[nodiscard]] Ex<void> send_dips_();
    [[nodiscard]] proto::DipChoice current_choice_(const mra::DipRow& d) const noexcept;

    StreamLoadHost stream_;
    std::string dip_name_;
    std::string nvm_name_;
    mra::SwitchTable switches_{};
    mra::DipWord dip_cur_{};
    mra::DipWord dip_saved_{};
    std::optional<mra::NvramSave> nvram_{};
};

std::unique_ptr<Core> make_arcade(const CoreProfile& p, const CoreGrant& g);

}
