// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_engine_stats.h"
#include "cores/cd_profile.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

class ThreeDoCddEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kDiscInfoBytes = 2048;

    ThreeDoCddEngine(const CdDialectFacts& facts, const os::IClock& c)
        : CdEngine(facts.profile.ticks, c, 255),
          facts_(facts) {}

    [[nodiscard]] Ex<void> build_disc_info(
        std::span<std::byte, kDiscInfoBytes> out,
        const svc::DiscGeometry& geom
    );

    [[nodiscard]] bool disc_loaded() const noexcept { return loaded_; }

private:
    enum class State : std::uint8_t { Idle, Open, Read, Pause, Stop };

    [[nodiscard]] Ex<void> do_accept(
        std::span<const std::uint16_t> words,
        CdDialectPorts& p
    ) override;
    [[nodiscard]] Ex<void> do_tick(CdDialectPorts& p) override;
    [[nodiscard]] Ex<void> do_push_sector(CdDialectPorts&) override;
    [[nodiscard]] Ex<void> do_notify_disc(bool loaded, const svc::DiscGeometry& geom) override;
    void do_reset_machine() noexcept override;
    bool do_streaming(const svc::DiscGeometry& geom) const noexcept override;
    DriveView do_drive_view(const svc::DiscGeometry& geom) const noexcept override;

    std::size_t do_command_word_count() const noexcept override { return 4; }

    std::optional<std::uint16_t> do_command_selector() const noexcept override {
        return std::nullopt;
    }

    void do_after_command_round() noexcept override {}

    std::size_t do_status_words(
        std::uint64_t frame,
        std::span<std::uint16_t, kMaxStatusWords> out
    ) const noexcept override;

    [[nodiscard]] CdEngineStats do_stats() const noexcept override { return stats_; }

    [[nodiscard]] Ex<std::size_t> do_disc_descriptor(
        std::span<std::byte> out,
        const svc::DiscGeometry& geom
    ) override;

    void command_exec_(const svc::DiscGeometry& geom);
    [[nodiscard]] Ex<void> update_(CdDialectPorts& p);
    void hint_at_(HeadLba lba, const svc::DiscGeometry& geom) noexcept;
    void read_data_(const svc::DiscGeometry& geom);

    [[nodiscard]] Ex<void> data_sector_send_(
        std::span<const std::uint8_t> header,
        CdDialectPorts& p
    );

    CdDialectFacts facts_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[svc::kCdDataSize] = {};
    std::uint8_t comm_[8] = {};
    State state_ = State::Open;

    HeadLba lba_{};
    SectorCount block_reads_{};
    svc::TrackIndex track_{};
    bool read_pend_ = false;
    bool loaded_ = false;
};

}
