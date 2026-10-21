// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_flow_query.h"
#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_transport.h"
#include "cores/cdwire.h"

namespace mister::cores {

class SegaCddEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    SegaCddEngine(const CdDialectPorts& ports, const os::IClock& c, ICdFlowQuery* flow)
        : CdEngine(ports.profile.ticks, c, 255),
          ports_(ports),
          flow_(flow) {}

    [[nodiscard]] bool egress_bound() const noexcept { return ports_.egress.has_value(); }

    bool flow_bound() const noexcept { return flow_ != nullptr; }

private:
    class CddaRead {
        TASTY_SEAT_RESIDENT(RT);

    public:
        [[nodiscard]] std::int32_t at(std::int32_t head) const noexcept { return head + lead_; }

        void locate() noexcept { lead_ = 0; }

        void reprime() noexcept { primed_ = false; }

        [[nodiscard]] std::int32_t frame_sectors() noexcept {
            const std::int32_t n = primed_ ? 1 : 2;
            primed_ = true;
            return n;
        }

        void read(std::int32_t n) noexcept { lead_ += n - 1; }

    private:
        std::int32_t lead_ = 0;
        bool primed_ = false;
    };

    struct Cdd {
        std::uint8_t status = 0x0B;
        std::uint8_t stat[9] = {0xB, 0, 0, 0, 0, 0, 0, 0, 0};
        std::uint8_t comm[10] = {};
        bool is_data = true;
        bool loaded = false;
        bool has_command = false;
        std::uint32_t latency = 10;
        std::int32_t lba = 0;
        CddaRead cdda{};
        std::uint32_t track = 0;
        std::int32_t scan_offset = 0;
    };

    [[nodiscard]] Ex<void> do_accept(std::span<const std::uint16_t> words) override;
    [[nodiscard]] Ex<void> do_tick() override;
    [[nodiscard]] Ex<void> do_push_sector() override;
    [[nodiscard]] Ex<void> do_notify_disc(bool loaded) override;
    void do_reset_machine() noexcept override;
    bool do_streaming() const noexcept override;

    DriveView do_drive_view() const noexcept override {
        return DriveView{st_.status, st_.track, st_.lba, st_.cdda.at(st_.lba), st_.is_data};
    }

    std::size_t do_command_word_count() const noexcept override { return 3; }

    std::optional<std::uint16_t> do_command_selector() const noexcept override {
        return cdwire::kCddGetCmd;
    }

    void do_after_command_round() noexcept override {}

    std::size_t do_status_words(
        std::uint64_t frame,
        std::span<std::uint16_t, kMaxStatusWords> out
    ) const noexcept override;

    [[nodiscard]] CdEngineStats do_stats() const noexcept override { return stats_; }

    [[nodiscard]] std::uint8_t do_speed_index() const noexcept override { return speed_index_; }

    [[nodiscard]] Ex<void> update_();
    [[nodiscard]] Ex<void> send_sector_();

    std::span<const std::uint8_t> subcode_stage_();
    void command_exec_();
    void seek_(std::int32_t lba, bool play);

    bool can_send_ready_(svc::TrackType type, bool skip_mode1);

    void hint_at_(std::int32_t lba) noexcept;
    void pump_pipeline_() noexcept;

    CdDialectPorts ports_;
    ICdFlowQuery* flow_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[2352 + 2352] = {};
    std::uint8_t sub_buf_[100] = {};
    Cdd st_{};

    std::uint8_t speed_index_ = 0;
};

}
