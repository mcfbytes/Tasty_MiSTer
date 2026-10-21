// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cdda_play_range.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_transport.h"
#include "cores/scsi_sense.h"

namespace mister::cores {

class PceScsiEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    PceScsiEngine(const CdDialectPorts& ports, const os::IClock& c)
        : CdEngine(ports.profile.ticks, c, 0),
          ports_(ports) {}

    void grant_read_ack() noexcept { st_.can_read_next = true; }

    struct View {
        std::uint8_t state = 0;
        std::uint32_t latency = 0;
        std::int32_t lba = 0;
        std::uint32_t cnt = 0;
        std::uint32_t track = 0;
        bool can_read_next = false;
        bool has_status = false;
    };

    View view() const noexcept {
        return View{
            st_.state,
            st_.latency,
            st_.lba,
            st_.cnt,
            st_.track,
            st_.can_read_next,
            st_.has_status
        };
    }

    [[nodiscard]] Ex<bool> drain_pending();

private:
    bool do_cart_load_ejects() const noexcept override { return true; }

    struct Cdd {
        std::uint8_t state = 1;
        std::uint8_t comm[14] = {};
        std::uint16_t stat = 0;
        bool loaded = false;
        bool has_status = false;
        bool data_req = false;
        bool can_read_next = false;
        bool int_pend = false;
        bool subq_pend = false;
        bool region = false;
        bool is_data = true;
        std::uint8_t cdda_mode = 0;
        std::uint8_t cdda_first = 0;
        std::uint32_t latency = 0;
        std::uint32_t audio_delay = 0;
        std::uint32_t cnt = 0;
        std::int32_t lba = 0;
        CddaPlayRange cdda{};
        std::uint32_t track = 0;
        ScsiSense sense{};
        std::uint8_t subq[12] = {};
        std::uint8_t subc[96] = {};
        std::int32_t subc_lba = -1;
    };

    [[nodiscard]] Ex<void> do_accept(std::span<const std::uint16_t> words) override;
    [[nodiscard]] Ex<void> do_tick() override;
    [[nodiscard]] Ex<void> do_push_sector() override;
    [[nodiscard]] Ex<void> do_notify_disc(bool loaded) override;
    void do_reset_machine() noexcept override;
    bool do_streaming() const noexcept override;

    DriveView do_drive_view() const noexcept override {
        return DriveView{st_.state, st_.track, st_.lba, st_.lba, st_.is_data};
    }

    std::size_t do_command_word_count() const noexcept override { return 7; }

    std::optional<std::uint16_t> do_command_selector() const noexcept override {
        return std::nullopt;
    }

    void do_after_command_round() noexcept override { static_cast<void>(apply_pending_reset()); }

    std::size_t do_status_words(
        std::uint64_t frame,
        std::span<std::uint16_t, kMaxStatusWords> out
    ) const noexcept override;

    void do_set_region(bool region) noexcept override { st_.region = region; }

    [[nodiscard]] CdEngineStats do_stats() const noexcept override { return stats_; }

    [[nodiscard]] Ex<void> update_();
    [[nodiscard]] Ex<void> read_send_();
    [[nodiscard]] Ex<void> command_exec_();
    [[nodiscard]] Ex<void> send_status_(std::uint16_t status);
    [[nodiscard]] Ex<void> send_data_request_();
    void build_subcode_frame_(std::int32_t lba);
    std::uint32_t track_by_lba_(std::int32_t lba) const;
    std::uint32_t seek_ticks_(std::int32_t from, std::int32_t to) const;

    void hint_at_(std::int32_t lba, bool doubled = false) noexcept;
    void pump_pipeline_() noexcept;

    CdDialectPorts ports_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[2352 + 2352] = {};
    std::uint8_t sub_buf_[100] = {};
    Cdd st_{};
};

}
