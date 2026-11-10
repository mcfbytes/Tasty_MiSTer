// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_io_indices.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/cdwire.h"
#include "cores/sega_cdd_stat.h"
#include "cores/sega_frame_word.h"
#include "cores/types.h"
#include "svc/types.h"

namespace mister::cores {

enum class SegaCddCommand : std::uint8_t {
    Idle = 0x0,
    Stop = 0x1,
    Toc = 0x2,
    Play = 0x3,
    Seek = 0x4,
    Pause = 0x6,
    Resume = 0x7,
    FwScan = 0x8,
    RwScan = 0x9,
    TrackMove = 0xA,
    TrackPlay = 0xB,
    TrayClose = 0xC,
    TrayOpen = 0xD,
};

inline constexpr std::array<SegaCddCommand, 13> kSegaCddCommands{
    SegaCddCommand::Idle,
    SegaCddCommand::Stop,
    SegaCddCommand::Toc,
    SegaCddCommand::Play,
    SegaCddCommand::Seek,
    SegaCddCommand::Pause,
    SegaCddCommand::Resume,
    SegaCddCommand::FwScan,
    SegaCddCommand::RwScan,
    SegaCddCommand::TrackMove,
    SegaCddCommand::TrackPlay,
    SegaCddCommand::TrayClose,
    SegaCddCommand::TrayOpen,
};

enum class TocSubmode : std::uint8_t {
    Position = 0,
    TrackRelative = 1,
    TrackNumber = 2,
    LeadOut = 3,
    FirstLast = 4,
    TrackInfo = 5,
    SubQ = 6,
};

namespace detail {

[[nodiscard]] constexpr std::optional<SegaCddCommand> sega_command_of(std::uint8_t raw) noexcept {
    for (const SegaCddCommand c : kSegaCddCommands)
        if (static_cast<std::uint8_t>(c) == raw)
            return c;
    return std::nullopt;
}

[[nodiscard]] constexpr TocSubmode toc_submode_of(std::uint8_t raw) noexcept {
    return static_cast<TocSubmode>(raw);
}

}

class SegaCddEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    SegaCddEngine(
        const CdDialectFacts& facts,
        const CdProfile::SegaCddFacts& row,
        const os::IClock& c,
        SectorIo io
    )
        : CdEngine(facts.profile.ticks, c, 255),
          facts_(facts),
          row_(row),
          io_(io) {}

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

    class Comm {
    public:
        enum : unsigned { kOpcodeNibble = 0, kBcd0TensNibble = 2, kTocSubmodeNibble = 3 };

        [[nodiscard]] static Comm of(SegaFrameWord frame) noexcept;

        [[nodiscard]] std::optional<SegaCddCommand> opcode() const noexcept { return opcode_; }

        [[nodiscard]] TocSubmode toc_submode() const noexcept { return toc_submode_; }

        [[nodiscard]] std::uint8_t digits(SegaCddStat::BcdSlot slot) const noexcept {
            return digits_[SegaCddStat::bcd_slot_index(slot)];
        }

        [[nodiscard]] cdwire::Msf msf() const noexcept {
            return cdwire::Msf{
                digits(SegaCddStat::BcdSlot::Bcd0),
                digits(SegaCddStat::BcdSlot::Bcd1),
                digits(SegaCddStat::BcdSlot::Bcd2)
            };
        }

    private:
        std::optional<SegaCddCommand> opcode_{};
        TocSubmode toc_submode_ = TocSubmode::Position;
        std::array<std::uint8_t, 3> digits_{};
    };

    struct Cdd {
        Cdd() noexcept { stat.set_status(SegaCddStatus::NoDisc); }

        SegaCddStatus status = SegaCddStatus::NoDisc;
        SegaCddStat stat{};
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
    void do_notify_disc(bool loaded, const svc::DiscGeometry& geom) noexcept override;
    void do_reset_machine() noexcept override;
    bool do_streaming(const svc::DiscGeometry& geom) const noexcept override;

    DriveView do_drive_view(const svc::DiscGeometry&) const noexcept override {
        return DriveView{
            .state = drive_state_of(st_.status, st_.is_data),
            .track = st_.track,
            .lba = st_.lba,
            .audio_lba = st_.cdda.at(st_.lba),
            .is_data = st_.is_data
        };
    }

    std::size_t do_command_word_count() const noexcept override { return 3; }

    std::optional<cdwire::Selector> do_command_selector() const noexcept override {
        return cdwire::kCddGetCmd;
    }

    void do_after_command_round() noexcept override {}

    [[nodiscard]] CdEngineStats do_stats() const noexcept override { return stats_; }

    [[nodiscard]] CdSpeedIndex do_speed_index() const noexcept override {
        return CdSpeedIndex{speed_index_};
    }

    [[nodiscard]] Ex<void> update_(CdDialectPorts& p);
    [[nodiscard]] Ex<void> send_sector_(CdDialectPorts& p);

    std::span<const std::uint8_t> subcode_stage_();
    void command_exec_(const Comm& command, const svc::DiscGeometry& geom);
    void seek_(HeadLba lba, bool play, const svc::DiscGeometry& geom);

    bool can_send_ready_(svc::TrackType type, bool skip_mode1, ICdLink& link);

    void hint_at_(HeadLba lba, const svc::DiscGeometry& geom) noexcept;
    void pump_pipeline_(const svc::DiscGeometry& geom) noexcept;

    static constexpr std::size_t kFramePairBytes = 2 * svc::kRawFrameBytes;
    static constexpr std::size_t kSubcodeStageBytes = 100;

    CdDialectFacts facts_;
    const CdProfile::SegaCddFacts& row_;

    SectorIo io_;
    CdEngineStats stats_{};

    std::array<std::uint8_t, kFramePairBytes> sec_buf_{};
    std::array<std::uint8_t, kSubcodeStageBytes> sub_buf_{};
    Cdd st_{};

    std::uint8_t speed_index_ = 0;
};

}
