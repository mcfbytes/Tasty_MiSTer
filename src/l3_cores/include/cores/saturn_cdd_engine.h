// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/core_window.h"
#include "cores/saturn_cdd_frame.h"
#include "cores/saturn_stock_toc.h"
#include "cores/saturn_mode.h"
#include "cores/types.h"
#include "cores/saturn_pending.h"
#include "cores/saturn_ring.h"
#include "cores/saturn_seek.h"
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

    SaturnCddEngine(const CdDialectFacts& facts, const os::IClock& c, ICoreWindow* window)
        : CdEngine(facts.profile.ticks, c, 255),
          facts_(facts),
          window_(window) {
        reset_status_();
    }

    [[nodiscard]] bool toc_at_index1() const noexcept { return toc_at_index1_; }

    [[nodiscard]] bool read_skips_seek() const noexcept { return read_skips_seek_; }

    [[nodiscard]] std::uint32_t declines() const noexcept { return declines_; }

    [[nodiscard]] bool header_owed() const noexcept { return header_owed_; }

    [[nodiscard]] std::span<const std::uint8_t, saturn::kFrameBytes> status_frame() const noexcept {
        return stat_.bytes;
    }

private:
    enum class State : std::uint8_t {
        Idle = 0,
        Open = 1,
        ReadToc = 2,
        Read = 3,
        Stop = 5,
        Seek = 6,
        SeekRing = 8,
    };

    enum class Sent : std::uint8_t { Yes, Declined };
    using StatField = SaturnCddFrame::Field;

    [[nodiscard]] Ex<void> do_accept(
        std::span<const std::uint16_t> words,
        CdDialectPorts& p
    ) override;
    [[nodiscard]] Ex<void> do_tick(CdDialectPorts& p) override;

    [[nodiscard]] Ex<void> do_push_sector(CdDialectPorts&) override { return {}; }

    [[nodiscard]] Ex<void> do_notify_disc(bool loaded, const svc::DiscGeometry& geom) override;
    void do_reset_machine() noexcept override;
    bool do_streaming(const svc::DiscGeometry& geom) const noexcept override;
    DriveView do_drive_view(const svc::DiscGeometry& geom) const noexcept override;

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

    [[nodiscard]] Ex<std::size_t> do_disc_descriptor(
        std::span<std::byte> out,
        const svc::DiscGeometry& geom
    ) override;

    void reset_status_() noexcept;
    void command_exec_(const svc::DiscGeometry& geom) noexcept;
    void process_(const svc::DiscGeometry& geom) noexcept;
    void fill_position_(
        std::uint8_t code,
        std::uint8_t q,
        bool pause_msf,
        const svc::DiscGeometry& geom
    ) noexcept;
    [[nodiscard]] Ex<void> update_(CdDialectPorts& p);
    [[nodiscard]] Ex<Sent> read_pass_(CdDialectPorts& p);
    [[nodiscard]] Ex<Sent> data_sector_send_(bool synth_header, CdDialectPorts& p);
    [[nodiscard]] Ex<Sent> audio_sector_send_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> ring_sector_send_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> send_mode_(SaturnModeWord mode, ICdLink& link);
    [[nodiscard]] bool take_header_(const svc::DiscGeometry& geom) noexcept;
    void prime_(const svc::DiscGeometry& geom) noexcept;
    void retrack_(const svc::DiscGeometry& geom) noexcept;

    [[nodiscard]] svc::TrackIndex track_by_lba_(
        HeadLba lba,
        const svc::DiscGeometry& geom
    ) const noexcept;
    [[nodiscard]] std::uint32_t index_by_lba_(svc::TrackIndex track, HeadLba lba) const noexcept;
    [[nodiscard]] SectorCount offset_by_index_(
        svc::TrackNumber tno,
        std::uint32_t idx,
        const svc::DiscGeometry& geom
    ) const noexcept;
    [[nodiscard]] HeadLba start_of_(svc::TrackIndex track) const noexcept;
    [[nodiscard]] bool is_data_(
        svc::TrackIndex track,
        const svc::DiscGeometry& geom
    ) const noexcept;
    [[nodiscard]] std::uint32_t sector_size_(
        svc::TrackIndex track,
        const svc::DiscGeometry& geom
    ) const noexcept;

    CdDialectFacts facts_;

    ICoreWindow* window_;
    CdEngineStats stats_{};

    SaturnCddFrame comm_{};
    SaturnCddFrame stat_{};
    State state_ = State::Open;
    HeadLba lba_{};
    svc::TrackIndex track_{};
    std::uint32_t index_ = 0;
    HeadLba seek_lba_{};
    std::uint32_t toc_pos_ = 0;
    saturn::SeekTicks seek_delay_{};
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

    struct AudioSector {
        std::array<std::uint8_t, saturn::kSectorBytes> data{};
        std::optional<HeadLba> lba{};
    };

    std::uint8_t sec_[saturn::kSectorBytes] = {};
    std::array<AudioSector, 2> aud_{};
    std::uint8_t header_[kBootHeaderBytes] = {};
};

}
