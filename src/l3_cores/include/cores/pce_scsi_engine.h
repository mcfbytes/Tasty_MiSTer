// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cdda_play_range.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/scsi_sense.h"
#include "cores/types.h"
#include "svc/types.h"

namespace mister::cores {

class PceScsiEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    PceScsiEngine(const CdDialectFacts& facts, const os::IClock& c)
        : CdEngine(facts.profile.ticks, c, 0),
          facts_(facts) {}

    void grant_read_ack() noexcept { st_.can_read_next = true; }

    struct View {
        std::uint8_t state = 0;
        std::uint32_t latency = 0;
        HeadLba lba{};
        std::uint32_t cnt = 0;
        svc::TrackIndex track{};
        bool can_read_next = false;
        bool has_status = false;
    };

    svc::TrackIndex head_track() const noexcept {
        return svc::TrackIndex{static_cast<std::uint8_t>(st_.track)};
    }

    View view() const noexcept {
        return View{
            st_.state,
            st_.latency,
            HeadLba{st_.lba},
            st_.cnt,
            head_track(),
            st_.can_read_next,
            st_.has_status
        };
    }

    [[nodiscard]] Ex<bool> drain_pending(ICdLink& link);

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

    [[nodiscard]] Ex<void> do_accept(
        std::span<const std::uint16_t> words,
        CdDialectPorts& p
    ) override;
    [[nodiscard]] Ex<void> do_tick(CdDialectPorts& p) override;
    [[nodiscard]] Ex<void> do_push_sector(CdDialectPorts& p) override;
    [[nodiscard]] Ex<void> do_notify_disc(bool loaded, const svc::DiscGeometry& geom) override;
    void do_reset_machine() noexcept override;
    bool do_streaming(const svc::DiscGeometry& geom) const noexcept override;

    DriveView do_drive_view(const svc::DiscGeometry&) const noexcept override {
        return DriveView{st_.state, head_track(), st_.lba, st_.lba, st_.is_data};
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

    [[nodiscard]] Ex<void> update_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> read_send_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> command_exec_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> send_status_(std::uint16_t status, ICdLink& link);
    [[nodiscard]] Ex<void> send_data_request_(ICdLink& link);
    void build_subcode_frame_(std::int32_t lba, const svc::DiscGeometry& geom);
    std::uint32_t track_by_lba_(std::int32_t lba, const svc::DiscGeometry& geom) const;
    std::uint32_t seek_ticks_(
        std::int32_t from,
        std::int32_t to,
        const svc::DiscGeometry& geom
    ) const;

    void hint_at_(std::int32_t lba, bool doubled, const svc::DiscGeometry& geom) noexcept;
    void pump_pipeline_(const svc::DiscGeometry& geom) noexcept;

    CdDialectFacts facts_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[2352 + 2352] = {};
    std::uint8_t sub_buf_[100] = {};
    Cdd st_{};
};

}
