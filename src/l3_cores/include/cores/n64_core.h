// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/cheat_engine.h"
#include "cores/core_support.h"
#include "cores/n64_cheat_engine.h"
#include "cores/n64_db_tags.h"
#include "cores/n64_file_route.h"
#include "cores/n64_options.h"
#include "cores/n64_saves.h"
#include "cores/stream_load.h"
#include "cores/window_job.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

class N64Core final : public Core,
                      public ICheatEngine,
                      public IStreamLoad,
                      public IWindowSave,
                      private n64::ISaveLayout {
    TASTY_SEAT_RESIDENT(RT);

public:
    N64Core(const CoreProfile& p, const HostServices& h);

    void service_cheat_tick() noexcept override;

    static constexpr IoIndex kPifIndex = n64::kPifIndex;
    static constexpr IoIndex kRomIndex = n64::kRomIndex;
    static constexpr IoIndex kGbIndex = n64::kGbIndex;
    static constexpr IoIndex kDdDiskIndex = n64::kDdDiskIndex;
    static constexpr IoIndex kDdIplIndex = n64::kDdIplIndex;

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }

    [[nodiscard]] std::uint32_t transfers() const noexcept { return transfers_; }

    [[nodiscard]] std::uint16_t last_index() const noexcept { return last_index_; }

    [[nodiscard]] const n64::CheatEngine& cheats() const noexcept { return cheats_; }

    [[nodiscard]] const n64::SaveChannel& saves() const noexcept { return saves_; }

    [[nodiscard]] std::uint32_t save_plan_errors() const noexcept { return save_plan_errors_; }

    [[nodiscard]] bool rom_loaded() const noexcept { return loaded_; }

    [[nodiscard]] bool turbo_core() const noexcept { return turbo_core_; }

    [[nodiscard]] std::uint32_t facts_refusals() const noexcept { return facts_refusals_; }

    [[nodiscard]] bool disk_bound() const noexcept { return disk_addr_ != 0; }

    [[nodiscard]] std::string_view disk_path() const noexcept { return disk_path_.view(); }

    [[nodiscard]] const IWindowJobKind* on_osd_open(std::span<std::byte> order) noexcept override;
    [[nodiscard]] const IWindowJobKind* on_reset_edge(
        const proto::ResetEdge& edge,
        std::span<std::byte> order
    ) noexcept override;
    [[nodiscard]] bool owes_flush(proto::IoIndex index) const noexcept override;
    [[nodiscard]] const IWindowJobKind* take_flush(
        proto::IoIndex index,
        std::span<std::byte> order
    ) noexcept override;

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> do_reset() override;

    [[nodiscard]] ICheatSink* on_cheat_sink() noexcept override { return &cheats_; }

    [[nodiscard]] ICheatEngine* on_cheat_engine() noexcept override { return this; }

    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    void on_set_pending_file_path(std::string_view path) noexcept override;

    [[nodiscard]] IStreamLoad* on_stream_load() noexcept override { return this; }

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] Ex<void> on_apply_load_facts(std::span<const std::uint8_t> facts) override;

    [[nodiscard]] ISaveChannel* on_save_channel() noexcept override { return &saves_; }

    [[nodiscard]] IWindowSave* on_window_save() noexcept override { return this; }

    [[nodiscard]] const IWindowJobKind* order_ram_save_(std::span<std::byte> order) noexcept;

    [[nodiscard]] std::uint32_t save_offset(std::size_t idx) const noexcept override;
    [[nodiscard]] std::uint8_t layout_key(std::size_t rows) const noexcept override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent_; }

    [[nodiscard]] std::uint32_t on_last_tx_crc() const noexcept override { return crc32_; }

    void plan_saves_();

    [[nodiscard]] std::uint32_t read_(n64::StatusField f) const noexcept;

    [[nodiscard]] bool cheats_enabled_() const noexcept;
    void disable_cheats_() noexcept;
    [[nodiscard]] n64::MemoryType cart_save_type_() const noexcept;

    infra::OptRef<proto::CoreSession> session_{};
    bool turbo_core_ = false;
    FixedStr<8, StrFit::Clip> ext_{};

    FixedStr<n64::SaveChannel::kPathChars + 1, StrFit::Clip> rom_path_{};
    std::uint32_t crc32_ = 0;
    std::uint64_t bytes_sent_ = 0;
    std::uint32_t transfers_ = 0;
    std::uint16_t last_index_ = 0;
    std::uint32_t save_plan_errors_ = 0;
    std::uint32_t facts_refusals_ = 0;
    std::uint32_t disk_addr_ = 0;
    FixedStr<n64::SaveChannel::kPathChars + 1, StrFit::Clip> disk_path_{};
    bool loaded_ = false;
    n64::CheatEngine cheats_;
    n64::SaveChannel saves_;
};

}
