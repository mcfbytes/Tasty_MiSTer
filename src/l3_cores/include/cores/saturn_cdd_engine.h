// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_profile.h"
#include "cores/cd_transport.h"
#include "cores/core_window.h"
#include "cores/saturn_stock_toc.h"
#include "cores/saturn_pending.h"
#include "cores/saturn_ring.h"
#include "cores/saturn_slot_ring.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/disc_read_service.h"
#include "svc/track.h"

namespace mister::cores {

class SaturnCddEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kSlotBytes = 4096;
    static constexpr std::size_t kSlots = 4;

    static constexpr std::size_t kBootHeaderBytes = 256;

    SaturnCddEngine(const CdDialectPorts& ports, const os::IClock& c, ICoreWindow* window)
        : CdEngine(ports.profile.ticks, c, 255),
          ports_(ports),
          window_(window) {
        reset_status_();
    }

    [[nodiscard]] bool toc_at_index1() const noexcept { return toc_at_index1_; }

    [[nodiscard]] bool read_skips_seek() const noexcept { return read_skips_seek_; }

    [[nodiscard]] std::uint32_t declines() const noexcept { return declines_; }

    [[nodiscard]] bool header_owed() const noexcept { return header_owed_; }

    [[nodiscard]] std::span<const std::uint8_t, saturn::kFrameBytes> status_frame() const noexcept {
        return std::span<const std::uint8_t, saturn::kFrameBytes>(stat_);
    }

private:
    enum class State : std::uint8_t {
        Idle,
        Open,
        ReadToc,
        Read,
        Pause,
        Stop,
        Seek,
        SeekRead,
        SeekRing
    };

    enum class Sent : std::uint8_t { Yes, Declined };

    [[nodiscard]] Ex<void> do_accept(std::span<const std::uint16_t> words) override;
    [[nodiscard]] Ex<void> do_tick() override;

    [[nodiscard]] Ex<void> do_push_sector() override { return {}; }

    [[nodiscard]] Ex<void> do_notify_disc(bool loaded) override;
    void do_reset_machine() noexcept override;
    bool do_streaming() const noexcept override;
    DriveView do_drive_view() const noexcept override;

    std::size_t do_command_word_count() const noexcept override { return 6; }

    std::optional<std::uint16_t> do_command_selector() const noexcept override {
        return std::nullopt;
    }

    void do_after_command_round() noexcept override {}

    std::size_t do_status_words(
        std::uint64_t frame,
        std::span<std::uint16_t, kMaxStatusWords> out
    ) const noexcept override;

    [[nodiscard]] CdEngineStats do_stats() const noexcept override { return stats_; }

    [[nodiscard]] std::uint8_t do_speed_index() const noexcept override {
        return static_cast<std::uint8_t>(speed_);
    }

    [[nodiscard]] Ex<std::size_t> do_disc_descriptor(std::span<std::byte> out) override;

    void reset_status_() noexcept;
    void command_exec_() noexcept;
    void process_() noexcept;
    void fill_position_(std::uint8_t code, std::uint8_t q, bool pause_msf) noexcept;
    [[nodiscard]] Ex<void> update_();
    [[nodiscard]] Ex<Sent> read_pass_();
    [[nodiscard]] Ex<Sent> data_sector_send_(bool synth_header);
    [[nodiscard]] Ex<Sent> audio_sector_send_();
    [[nodiscard]] Ex<void> ring_sector_send_();
    [[nodiscard]] Ex<void> send_mode_(std::uint16_t mode);
    [[nodiscard]] bool take_header_() noexcept;
    void prime_() noexcept;
    void retrack_() noexcept;

    [[nodiscard]] std::int32_t track_by_lba_(std::int32_t lba) const noexcept;
    [[nodiscard]] std::int32_t index_by_lba_(std::int32_t track, std::int32_t lba) const noexcept;
    [[nodiscard]] std::int32_t offset_by_index_(std::int32_t tno, std::int32_t idx) const noexcept;
    [[nodiscard]] std::int32_t start_of_(std::int32_t track) const noexcept;
    [[nodiscard]] bool is_data_(std::int32_t track) const noexcept;
    [[nodiscard]] std::uint32_t sector_size_(std::int32_t track) const noexcept;

    CdDialectPorts ports_;

    ICoreWindow* window_;
    CdEngineStats stats_{};

    std::uint8_t comm_[saturn::kFrameBytes] = {};
    std::uint8_t stat_[saturn::kFrameBytes] = {};
    State state_ = State::Open;
    std::int32_t lba_ = 0;
    std::int32_t track_ = 0;
    std::int32_t index_ = 0;
    std::int32_t seek_lba_ = 0;
    std::int32_t toc_pos_ = 0;
    std::int32_t seek_delay_ = 0;
    std::int32_t speed_ = 0;
    bool lid_open_ = true;
    SaturnPending pend_{};
    bool audio_first_ = false;

    std::int32_t seek_ring_time_ = 1;

    SaturnSlotRing data_ring_{};
    SaturnSlotRing audio_ring_{};

    SaturnStockToc stock_toc_{};

    bool toc_at_index1_ = false;
    bool read_skips_seek_ = false;
    bool header_owed_ = false;
    std::uint32_t declines_ = 0;

    std::uint8_t sec_[saturn::kSectorBytes] = {};
    std::uint8_t aud_[2][saturn::kSectorBytes] = {};

    std::int64_t aud_lba_[2] = {-1, -1};
    std::uint8_t header_[kBootHeaderBytes] = {};
};

}
