// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/cdwire.h"
#include "cores/sega_cdd_stat.h"
#include "cores/types.h"
#include "svc/types.h"

namespace mister::cores {

class SegaCddEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    SegaCddEngine(const CdDialectFacts& facts, const os::IClock& c)
        : CdEngine(facts.profile.ticks, c, 255),
          facts_(facts) {}

private:
    class CddaRead {
        TASTY_SEAT_RESIDENT(RT);

    public:
        [[nodiscard]] HeadLba at(HeadLba head) const noexcept { return head + lead_; }

        void locate() noexcept { lead_ = SectorCount{}; }

        void reprime() noexcept { primed_ = false; }

        [[nodiscard]] SectorCount frame_sectors() noexcept {
            const SectorCount n = primed_ ? SectorCount{1} : SectorCount{2};
            primed_ = true;
            return n;
        }

        void read(SectorCount n) noexcept { lead_ += n - SectorCount{1}; }

    private:
        SectorCount lead_{};
        bool primed_ = false;
    };

    struct Cdd {
        static constexpr std::uint8_t kNoDisc = 0x0B;

        Cdd() noexcept { stat.set_status(kNoDisc); }

        std::uint8_t status = kNoDisc;
        SegaCddStat stat{};
        std::uint8_t comm[10] = {};
        bool is_data = true;
        bool loaded = false;
        bool has_command = false;
        std::uint32_t latency = 10;
        HeadLba lba{};
        CddaRead cdda{};
        svc::TrackIndex track{};
        SectorCount scan_offset{};
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

    [[nodiscard]] Ex<void> update_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> send_sector_(CdDialectPorts& p);

    std::span<const std::uint8_t> subcode_stage_();
    void command_exec_(const svc::DiscGeometry& geom);
    void seek_(HeadLba lba, bool play, const svc::DiscGeometry& geom);

    bool can_send_ready_(svc::TrackType type, bool skip_mode1, ICdLink& link);

    void hint_at_(HeadLba lba, const svc::DiscGeometry& geom) noexcept;
    void pump_pipeline_(const svc::DiscGeometry& geom) noexcept;

    static constexpr std::size_t kFramePairBytes = 2 * svc::kCdDataSize;
    static constexpr std::size_t kSubcodeStageBytes = 100;

    CdDialectFacts facts_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[kFramePairBytes] = {};
    std::uint8_t sub_buf_[kSubcodeStageBytes] = {};
    Cdd st_{};

    std::uint8_t speed_index_ = 0;
};

}
