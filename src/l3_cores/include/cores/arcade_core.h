// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/arcade_facts.h"
#include "cores/core_grant.h"
#include "cores/core_support.h"
#include "cores/cheat_records.h"
#include "cores/dip_switches.h"
#include "cores/generic_core.h"
#include "cores/mra_cheat_records.h"
#include "cores/mra_doc.h"
#include "cores/save_upload.h"
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

class ArcadeCore final : public Core,
                         public IDipSwitches,
                         public ICheatRecords,
                         public ISaveUpload {
    TASTY_SEAT_RESIDENT(RT);

public:
    ArcadeCore(const CoreProfile& profile, const HostServices& host, ArcadeFacts facts);

    [[nodiscard]] std::size_t dip_count() const noexcept override;
    [[nodiscard]] DipRowView dip_row(std::size_t row) const noexcept override;
    [[nodiscard]] std::string_view dip_choice(
        std::size_t row,
        std::size_t choice
    ) const noexcept override;
    [[nodiscard]] Ex<void> set_dip(std::size_t row, std::size_t choice) override;

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

    [[nodiscard]] CheatGeometry cheat_geometry() const noexcept override {
        TASTY_SEAT_BODY(ArcadeCore);
        return cheats_.cheat_geometry();
    }

    [[nodiscard]] std::size_t cheat_count() const noexcept override {
        TASTY_SEAT_BODY(ArcadeCore);
        return cheats_.cheat_count();
    }

    [[nodiscard]] std::string_view cheat_name(std::size_t i) const noexcept override;
    [[nodiscard]] std::span<const std::uint8_t> cheat_bytes(std::size_t i) const noexcept override;

    [[nodiscard]] std::uint32_t cheats_dropped() const noexcept { return cheats_dropped_; }

    [[nodiscard]] std::string_view save_file_name() const noexcept override {
        TASTY_SEAT_BODY(ArcadeCore);
        return nvm_name_;
    }

    [[nodiscard]] Ex<std::span<const std::uint8_t>> pull_save() override;

    [[nodiscard]] std::uint32_t nvram_saves() const noexcept { return nvram_saves_; }

    [[nodiscard]] std::string_view nvram_file_name() const noexcept { return nvm_name_; }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt&) override { return {}; }

    [[nodiscard]] IStreamLoad* on_stream_load() noexcept override { return &file_host_; }

    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    std::uint64_t on_last_tx_bytes() const noexcept override;
    std::uint32_t on_last_tx_crc() const noexcept override;

    ButtonOverride on_button_override() const noexcept override {
        return {btn_names_, btn_defaults_};
    }

    IDipSwitches* on_dip_switches() noexcept override { return this; }

    ISaveUpload* on_save_upload() noexcept override { return nvram_declared_() ? this : nullptr; }

    ICheatRecords* on_cheat_records() noexcept override { return this; }

    [[nodiscard]] Ex<void> on_osd_closed() override { return {}; }

    [[nodiscard]] Ex<void> send_dips_();
    [[nodiscard]] bool nvram_declared_() const noexcept;
    [[nodiscard]] std::uint8_t current_choice_(const mra::DipRow& d) const noexcept;

    GenericCore file_host_;
    std::string btn_names_;
    std::string btn_defaults_;
    std::string dip_name_;
    std::string nvm_name_;
    mra::SwitchTable switches_{};
    mra::MraCheatRecords cheats_{};
    std::uint32_t cheats_dropped_ = 0;
    std::uint64_t dip_cur_ = 0;
    std::uint64_t dip_saved_ = 0;
    int nvram_index_ = 0;
    int nvram_size_ = 0;
    std::uint32_t nvram_saves_ = 0;
    std::vector<std::uint8_t> nvm_image_;
};

std::unique_ptr<Core> make_arcade(const CoreProfile& p, const CoreGrant& g);

}
