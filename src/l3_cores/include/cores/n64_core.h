// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/cheat_engine.h"
#include "cores/core_support.h"
#include "cores/n64_cheat_engine.h"
#include "cores/n64_file_route.h"
#include "cores/n64_layout_key_source.h"
#include "cores/n64_options.h"
#include "cores/n64_saves.h"
#include "cores/save_rows.h"
#include "cores/stream_load.h"
#include "cores/window_job.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

class N64Core final : public Core,
                      public ICheatEngine,
                      public IStreamLoad,
                      public IWindowSave,
                      private n64::ILayoutKeySource {
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

    [[nodiscard]] proto::IoIndex last_index() const noexcept { return last_index_; }

    [[nodiscard]] const n64::CheatEngine& cheats() const noexcept { return cheats_; }

    [[nodiscard]] const n64::SaveChannel& saves() const noexcept { return saves_; }

    [[nodiscard]] std::uint32_t save_plan_errors() const noexcept { return save_plan_errors_; }

    [[nodiscard]] bool rom_loaded() const noexcept { return loaded_; }

    [[nodiscard]] bool turbo_core() const noexcept { return turbo_core_; }

    [[nodiscard]] std::uint32_t facts_refusals() const noexcept { return facts_refusals_; }

    [[nodiscard]] bool disk_bound() const noexcept { return disk_addr_ != hal::FabricAddr{}; }

    [[nodiscard]] std::string_view disk_path() const noexcept { return disk_path_.view(); }

    [[nodiscard]] WindowJobBuild on_osd_open(WindowJobOrder& order) noexcept override;
    [[nodiscard]] WindowJobBuild on_reset_edge(
        const proto::ResetEdge& edge,
        WindowJobOrder& order
    ) noexcept override;
    [[nodiscard]] bool owes_flush(proto::IoIndex index) const noexcept override;
    [[nodiscard]] WindowJobBuild take_flush(
        proto::IoIndex index,
        WindowJobOrder& order
    ) noexcept override;

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> do_reset() override;

    [[nodiscard]] ICheatSink& on_cheat_sink() noexcept override { return cheats_; }

    [[nodiscard]] infra::OptRef<ICheatEngine> on_cheat_engine() noexcept override {
        return infra::OptRef<ICheatEngine>{*this};
    }

    void on_status_changed() noexcept override;
    void on_set_pending_file_ext(std::string_view ext) noexcept override;
    void on_set_pending_file_path(std::string_view path) noexcept override;

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{*this};
    }

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(IoIndex index) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] Ex<void> on_apply_load_facts(std::span<const std::uint8_t> facts) override;

    [[nodiscard]] infra::OptRef<ISaveChannel> on_save_channel() noexcept override {
        return infra::OptRef<ISaveChannel>{saves_};
    }

    [[nodiscard]] infra::OptRef<IWindowSave> on_window_save() noexcept override {
        return infra::OptRef<IWindowSave>{*this};
    }

    [[nodiscard]] WindowJobBuild order_ram_save_(WindowJobOrder& order) noexcept;

    [[nodiscard]] n64::SaveLayoutKey layout_key(n64::SaveRowCount rows) const noexcept override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent_; }

    [[nodiscard]] svc::Crc32 on_last_tx_crc() const noexcept override { return crc32_; }

    void adopt_save_plan_(const n64::SaveRows& plan);

    [[nodiscard]] proto::FieldValue read_(n64::StatusField f) const noexcept;

    [[nodiscard]] bool cheats_enabled_() const noexcept;
    void disable_cheats_() noexcept;

    infra::OptRef<proto::CoreSession> session_{};
    bool turbo_core_ = false;
    FixedStr<8, StrFit::Clip> ext_{};

    FixedStr<n64::SaveChannel::kPathChars + 1, StrFit::Clip> rom_path_{};
    svc::Crc32 crc32_{};
    std::uint64_t bytes_sent_ = 0;
    std::uint32_t transfers_ = 0;
    proto::IoIndex last_index_{};
    std::uint32_t save_plan_errors_ = 0;
    std::uint32_t facts_refusals_ = 0;
    hal::FabricAddr disk_addr_{};
    FixedStr<n64::SaveChannel::kPathChars + 1, StrFit::Clip> disk_path_{};
    bool loaded_ = false;
    n64::CheatEngine cheats_;
    n64::SaveChannel saves_;
};

}
