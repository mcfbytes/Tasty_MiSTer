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
#include "cores/cd_sector_egress.h"
#include "cores/cd_transport.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

class ThreeDoCddEngine final : public CdEngine {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kDiscInfoBytes = 2048;

    ThreeDoCddEngine(const CdDialectPorts& ports, const os::IClock& c)
        : CdEngine(ports.profile.ticks, c, 255),
          ports_(ports) {}

    [[nodiscard]] Ex<void> build_disc_info(std::span<std::byte, kDiscInfoBytes> out);

    [[nodiscard]] bool disc_loaded() const noexcept { return loaded_; }

private:
    enum class State : std::uint8_t { Idle, Open, Read, Pause, Stop };

    [[nodiscard]] Ex<void> do_accept(std::span<const std::uint16_t> words) override;
    [[nodiscard]] Ex<void> do_tick() override;
    [[nodiscard]] Ex<void> do_push_sector() override;
    [[nodiscard]] Ex<void> do_notify_disc(bool loaded) override;
    void do_reset_machine() noexcept override;
    bool do_streaming() const noexcept override;
    DriveView do_drive_view() const noexcept override;

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

    [[nodiscard]] Ex<std::size_t> do_disc_descriptor(std::span<std::byte> out) override;

    void command_exec_();
    [[nodiscard]] Ex<void> update_();
    void hint_at_(std::int32_t lba) noexcept;
    void read_data_();

    [[nodiscard]] Ex<void> data_sector_send_(std::span<const std::uint8_t> header);
    [[nodiscard]] std::uint32_t track_by_lba_(std::int32_t lba) const noexcept;

    CdDialectPorts ports_;
    CdEngineStats stats_{};

    std::uint8_t sec_buf_[2352] = {};
    std::uint8_t comm_[8] = {};
    State state_ = State::Open;

    std::int32_t lba_ = 0;
    std::int32_t block_reads_ = 0;
    std::uint32_t track_ = 0;
    bool read_pend_ = false;
    bool loaded_ = false;
};

}
