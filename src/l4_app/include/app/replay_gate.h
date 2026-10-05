// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/core_frame_counter.h"
#include "app/replay_control.h"
#include "app/replay_msg.h"
#include "app/replay_status.h"
#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/frame_counter_read.h"
#include "proto/joystick.h"
#include "proto/late_answers.h"
#include "proto/link_op.h"
#include "reactor/frame_clock.h"
#include "reactor/tick.h"

namespace mister::svc {
class InputEmitter;
}

namespace mister::app {

class ReplayGate {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        hal::ISpiTransport* link = nullptr;
        CoreFrameCounter* frames = nullptr;
        svc::InputEmitter* emitter = nullptr;
        const os::IClock* clock = nullptr;
        const ReplayControlCell* control = nullptr;
        ReplayStatusCell* status = nullptr;
        const reactor::FrameCell* vsync = nullptr;
    };

    static constexpr unsigned kReplayBudget = 8;

    static constexpr std::uint32_t kMaxRoundWords =
        2 * proto::FrameCounterRead::kReadWords +
        (kReplayBudget + 2) * kReplayPorts * proto::JoystickPort::kMaxDigitalPushWords;

    static constexpr std::uint32_t kAliasFrames = 200;

    static constexpr std::int64_t kEndSlackNs = 50'000;

    static constexpr std::uint32_t kFloorBack = 1;

    static constexpr std::int64_t kSilenceSlackNs = 2 * reactor::kTickNs;
    static constexpr std::int64_t kSilenceWindowNs = 1'000'000'000;

    static constexpr std::int64_t kSlipNs = 2 * reactor::kTickNs;

    static constexpr std::int64_t kGapNs = 2 * reactor::kTickNs;

    explicit ReplayGate(const Wiring& w) noexcept : w_(w) {}
    ReplayGate(const ReplayGate&) = delete;
    ReplayGate& operator=(const ReplayGate&) = delete;

    void tick(bool ready, std::uint32_t core_edge_seq, proto::LateAnswers late = {},
              std::optional<std::uint32_t> queued = std::nullopt) noexcept;
    [[nodiscard]] bool wants_record() const noexcept;

    [[nodiscard]] std::uint32_t take_round_words() noexcept {
        const std::uint32_t w = round_words_;
        round_words_ = 0;
        return w;
    }
    void on(const ReplayMsg::Arm& a, const ReplayMsg::Head& h) noexcept;
    void on(const ReplayMsg::Input& r, const ReplayMsg::Head& h) noexcept;
    void on(const ReplayMsg::End& e, const ReplayMsg::Head& h) noexcept;
    void misrouted(const ReplayMsg&) noexcept;

    void settle() noexcept;

    void note_download_end(std::uint8_t wire_index) noexcept;

    void note_core_reset() noexcept;

    [[nodiscard]] bool suppresses(const proto::LinkOp& op) noexcept;

    [[nodiscard]] bool armed() const noexcept { return level_ != ReplayLevel::Idle; }
    [[nodiscard]] const ReplayStatus& status() const noexcept { return status_; }

private:
    void sample_control_() noexcept;
    [[nodiscard]] bool sample_ref_() noexcept;
    void begin_run_() noexcept;
    void decide_epoch_(std::uint32_t delta) noexcept;
    void decide_silence_(std::uint32_t delta) noexcept;
    [[nodiscard]] bool due_(const ReplayMsg::Input& r) const noexcept;
    void apply_(const ReplayMsg::Input& r) noexcept;
    void write_(const ReplayMsg::Input& r) noexcept;
    void write_masks_(const std::array<std::uint32_t, kReplayPorts>& mask,
                      std::uint8_t ports) noexcept;
    void force_release_(std::uint8_t ports) noexcept;
    void note_late_(std::uint32_t first) noexcept;
    void note_gap_() noexcept;
    void note_depth_() noexcept;
    void disarm_(ReplayEnd why, bool release) noexcept;
    [[nodiscard]] bool pre_epoch_(std::uint32_t frame) const noexcept;
    [[nodiscard]] bool live_() const noexcept;
    [[nodiscard]] std::int64_t now_ns_() const noexcept;

    [[nodiscard]] Ex<proto::FrameCount> read_counter_() noexcept;

    Wiring w_;
    ReplayControlCell::Reader control_seen_{};
    ReplayControl ctrl_{};
    ReplayStatus status_{};
    bool dirty_ = false;

    ReplayLevel level_ = ReplayLevel::Idle;
    std::uint16_t gen_ = 0;
    std::uint8_t ports_ = 0;

    std::uint8_t release_ports_ = 0;
    std::uint8_t rom_index_ = 0xFF;
    bool vsync_ok_ = false;
    std::int32_t lead_ = 0;
    std::int64_t offset_ns_ = 0;
    std::int64_t period_ns_ = 0;
    std::int64_t line0_ns_ = 0;
    std::int64_t poweron_ns_ = 0;
    std::uint8_t late_frames_ = 0;
    ReplayMsg::P0Parity p0_ = ReplayMsg::P0Parity::Any;
    ReplayMsg::PowerOnEvent event_ = ReplayMsg::PowerOnEvent::LoadEnd;
    std::uint32_t floor_back_ = kFloorBack;
    std::int64_t alias_ns_ = 0;
    std::uint32_t edge_seq_ = 0;

    std::uint32_t last_raw_ = 0;
    std::uint32_t wrap_mask_ = 0;
    std::uint32_t frames_ = 0;
    std::int64_t edge_ns_ = 0;
    std::int64_t last_sample_ns_ = 0;
    std::int64_t end_ns_ = 0;
    std::int64_t prev_edge_ns_ = 0;
    std::int64_t read_ns_ = 0;
    std::int64_t edge_gap_ns_ = 0;
    std::uint32_t blk_seen_ = 0;
    std::uint32_t blk_base_ = 0;
    bool epoch_known_ = false;
    std::uint32_t epoch_ = 0;

    std::int64_t now_ = 0;
    bool ready_ = false;
    std::uint32_t round_words_ = 0;
    std::optional<std::uint32_t> queued_{};
    std::int64_t tick_ns_ = 0;
    std::int64_t gap_ns_ = 0;
    std::int64_t mf_ = -1;
    std::int64_t covered_ = -1;
    std::int64_t underrun_at_ = -1;
    std::optional<ReplayMsg::Input> held_{};
    bool end_seen_ = false;
    std::uint32_t end_frame_ = 0;
};

}  // namespace mister::app
