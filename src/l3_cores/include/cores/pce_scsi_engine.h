// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_io_indices.h"
#include "cores/cdda_play_range.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/pce_command.h"
#include "cores/poll_ticks.h"
#include "cores/scsi_sense.h"
#include "cores/types.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::cores {

class PceScsiEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    enum class State : std::uint8_t { Idle = 0, NoDisc = 1, Read = 2, Play = 3, Pause = 4 };

    PceScsiEngine(
        const CdDialectFacts& facts,
        const CdProfile::PceScsiFacts& row,
        const os::IClock& c,
        SectorIo io
    )
        : CdEngine(facts.profile.ticks, c, 0),
          facts_(facts),
          row_(row),
          io_(io) {}

    void grant_read_ack() noexcept { st_.can_read_next = true; }

    struct View {
        State state = State::Idle;
        PollTicks latency{};
        HeadLba lba{};
        SectorCount cnt{};
        svc::TrackIndex track{};
        bool can_read_next = false;
        bool has_status = false;
    };

    svc::TrackIndex head_track() const noexcept { return st_.track; }

    View view() const noexcept {
        return View{
            st_.state,
            st_.latency,
            st_.lba,
            st_.cnt,
            head_track(),
            st_.can_read_next,
            st_.has_status
        };
    }

    [[nodiscard]] Ex<bool> drain_pending(ICdLink& link);

private:
    enum class Status : std::uint16_t { Good = 0, CheckCondition = 1 };

    [[nodiscard]] static constexpr std::uint16_t word_of(Status s) noexcept {
        return static_cast<std::uint16_t>(s);
    }

    bool do_cart_load_ejects() const noexcept override { return true; }

    struct Cdd {
        State state = State::NoDisc;
        bool loaded = false;
        bool has_status = false;
        bool data_req = false;
        bool can_read_next = false;
        bool int_pend = false;
        bool subq_pend = false;
        bool region = false;
        bool is_data = true;
        std::optional<PceCommand::CddaMode> cdda_mode = PceCommand::CddaMode::Silent;
        bool cdda_first = false;
        PollTicks latency{};
        PollTicks audio_delay{};
        SectorCount cnt{};
        HeadLba lba{};
        CddaPlayRange cdda{};
        svc::TrackIndex track{};
        ScsiSense sense{};
        std::uint8_t subc[96] = {};
        HeadLba subc_lba{-1};
    };

    [[nodiscard]] Ex<void> do_accept(
        std::span<const std::uint16_t> words,
        CdDialectPorts& p
    ) override;
    [[nodiscard]] Ex<void> do_tick(CdDialectPorts& p) override;
    [[nodiscard]] Ex<void> do_push_sector(CdDialectPorts& p) override;
    void do_notify_disc(bool loaded, const svc::DiscGeometry& geom) noexcept override;
    void do_reset_machine() noexcept override;
    bool do_streaming(const svc::DiscGeometry& geom) const noexcept override;

    DriveView do_drive_view(const svc::DiscGeometry&) const noexcept override {
        return DriveView{
            .state = drive_state_(),
            .track = head_track(),
            .lba = st_.lba,
            .audio_lba = st_.lba,
            .is_data = st_.is_data
        };
    }

    [[nodiscard]] CdDriveState drive_state_() const noexcept;

    std::size_t do_command_word_count() const noexcept override { return PceCommand::kWords; }

    std::optional<cdwire::Selector> do_command_selector() const noexcept override {
        return std::nullopt;
    }

    void do_after_command_round() noexcept override { static_cast<void>(apply_pending_reset()); }

    void do_set_region(bool region) noexcept override { st_.region = region; }

    [[nodiscard]] CdEngineStats do_stats() const noexcept override { return stats_; }

    [[nodiscard]] Ex<void> update_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> read_send_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> deliver_cdda_pass_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> command_exec_(const PceCommand& command, CdDialectPorts& p);
    [[nodiscard]] Ex<void> send_status_(Status status, ICdLink& link);
    [[nodiscard]] Ex<void> send_data_request_(ICdLink& link);
    void build_subcode_frame_(HeadLba lba, const svc::DiscGeometry& geom);
    svc::TrackIndex track_by_lba_(HeadLba lba, const svc::DiscGeometry& geom) const;
    PollTicks seek_ticks_(HeadMove move, const svc::DiscGeometry& geom) const;

    void hint_at_(HeadLba lba, bool doubled, const svc::DiscGeometry& geom) noexcept;
    void pump_pipeline_(const svc::DiscGeometry& geom) noexcept;

    static constexpr std::size_t kFramePairBytes = 2 * svc::kRawFrameBytes;
    static constexpr std::size_t kSubcodeStageBytes = 100;

    CdDialectFacts facts_;
    const CdProfile::PceScsiFacts& row_;

    SectorIo io_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[kFramePairBytes] = {};
    std::uint8_t sub_buf_[kSubcodeStageBytes] = {};
    Cdd st_{};
};

}
