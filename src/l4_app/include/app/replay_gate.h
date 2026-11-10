// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "app/core_frame_counter.h"
#include "app/replay_control.h"
#include "app/replay_msg.h"
#include "app/replay_status.h"
#include "hal/spi_transport.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/frame_counter_read.h"
#include "proto/joystick_port.h"
#include "proto/late_answers.h"
#include "proto/link_op.h"
#include "proto/spi_frame.h"
#include "proto/types.h"
#include "reactor/frame_clock.h"
#include "reactor/tick.h"
#include "svc/types.h"

namespace mister::svc {
class InputEmitter;
}

namespace mister::app {

class ReplayGate {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        hal::ISpiTransport& link;
        CoreFrameCounter& frames;
        svc::InputEmitter& emitter;
        const os::IClock& clock;
        const ReplayControlCell& control;
        ReplayStatusCell& status;
        infra::OptRef<const reactor::FrameCell> vsync{};
    };

    static constexpr unsigned kReplayBudget = 8;

    static constexpr std::uint32_t kMaxRoundWords = 2 * proto::FrameCounterRead::kReadWords
        + (kReplayBudget + 2) * kReplayPorts * proto::JoystickPort::kMaxDigitalPushWords;

    static constexpr std::uint32_t kAliasFrames = 200;

    static constexpr std::chrono::nanoseconds kTickNs{reactor::kTickNs};

    static constexpr std::chrono::nanoseconds kEndSlackNs{50'000};

    static constexpr std::uint32_t kFloorBack = 1;

    static constexpr std::chrono::nanoseconds kSilenceSlackNs = 2 * kTickNs;
    static constexpr std::chrono::nanoseconds kSilenceWindowNs{1'000'000'000};

    static constexpr std::chrono::nanoseconds kSlipNs = 2 * kTickNs;

    static constexpr std::chrono::nanoseconds kGapNs = 2 * kTickNs;

    explicit ReplayGate(const Wiring& w) noexcept : w_(w) {}

    ReplayGate(const ReplayGate&) = delete;
    ReplayGate& operator=(const ReplayGate&) = delete;

    void tick(
        bool ready,
        CoreEdgeSeq core_edge_seq,
        proto::LateAnswers late = {},
        std::optional<std::uint32_t> queued = std::nullopt
    ) noexcept;
    [[nodiscard]] bool wants_record() const noexcept;

    [[nodiscard]] proto::FioWordCount take_round_words() noexcept {
        const std::uint32_t w = round_words_;
        round_words_ = 0;
        return proto::FioWordCount{w};
    }

    void on(const ReplayMsg::Arm& a, const ReplayMsg::Head& h) noexcept;
    void on(const ReplayMsg::Input& r, const ReplayMsg::Head& h) noexcept;
    void on(const ReplayMsg::End& e, const ReplayMsg::Head& h) noexcept;
    void misrouted(const ReplayMsg&) noexcept;

    void settle() noexcept;

    void note_download_end(proto::IoIndex wire_index) noexcept;

    void note_core_reset() noexcept;

    [[nodiscard]] bool suppresses(const proto::LinkOp& op) noexcept;

    [[nodiscard]] bool armed() const noexcept { return level_ != ReplayLevel::Idle; }

    [[nodiscard]] const ReplayStatus& status() const noexcept { return status_; }

private:
    struct EpochPick {
        std::int64_t j;
        std::int64_t hi;
    };

    struct EpochRule {
        std::chrono::nanoseconds poweron{};
        std::chrono::nanoseconds period{};
        std::chrono::nanoseconds line0{};
        ReplayMsg::P0Parity p0 = ReplayMsg::P0Parity::Any;
        cores::LateFrames late_frames{};

        [[nodiscard]] EpochPick pick(
            std::chrono::nanoseconds delay,
            std::uint32_t first_count
        ) const noexcept;
    };

    void sample_control_() noexcept;
    [[nodiscard]] bool sample_ref_() noexcept;
    void begin_run_() noexcept;
    void decide_epoch_(std::uint32_t delta) noexcept;
    void decide_silence_(std::uint32_t delta) noexcept;
    [[nodiscard]] bool due_(const ReplayMsg::Input& r) const noexcept;
    void apply_(const ReplayMsg::Input& r) noexcept;
    void write_(const ReplayMsg::Input& r) noexcept;
    void write_masks_(
        const std::array<proto::JoyMask, kReplayPorts>& mask,
        svc::PlayerMask ports
    ) noexcept;
    void force_release_(svc::PlayerMask ports) noexcept;
    void count_write_(const Ex<bool>& out, std::uint32_t held_before) noexcept;
    void note_late_(MovieFrame first) noexcept;
    void note_gap_() noexcept;
    void note_depth_() noexcept;
    void disarm_(ReplayEnd why, bool release) noexcept;
    [[nodiscard]] bool pre_epoch_(MovieFrame frame) const noexcept;
    [[nodiscard]] bool live_() const noexcept;

    [[nodiscard]] std::chrono::nanoseconds end_to_edge_() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds into_frame_() const noexcept;

    [[nodiscard]] std::chrono::nanoseconds since_offset_() const noexcept;

    [[nodiscard]] Ex<proto::FrameCount> read_counter_() noexcept;

    Wiring w_;

    proto::SpiFrame frame_{w_.link};
    ReplayControlCell::Reader control_seen_{};
    ReplayControl ctrl_{};
    ReplayStatus status_{};
    bool dirty_ = false;

    ReplayLevel level_ = ReplayLevel::Idle;
    ControlGeneration gen_{};
    svc::PlayerMask ports_{};

    svc::PlayerMask release_ports_{};
    proto::IoIndex rom_index_ = kNoRomSlot;
    bool vsync_ok_ = false;
    MovieSpan lead_{};
    std::chrono::nanoseconds offset_ns_{};

    EpochRule rule_{};
    ReplayMsg::PowerOnEvent event_ = ReplayMsg::PowerOnEvent::LoadEnd;
    std::uint32_t floor_back_ = kFloorBack;
    std::chrono::nanoseconds alias_ns_{};
    CoreEdgeSeq edge_seq_{};

    std::uint32_t last_raw_ = 0;
    std::uint32_t wrap_mask_ = 0;
    std::uint32_t frames_ = 0;
    os::RawMonotonicClock::time_point edge_{};
    os::RawMonotonicClock::time_point last_sample_{};
    os::RawMonotonicClock::time_point end_{};
    os::RawMonotonicClock::time_point prev_edge_{};
    std::optional<os::RawMonotonicClock::time_point> read_{};
    std::chrono::nanoseconds edge_gap_{};
    std::uint32_t blk_seen_ = 0;
    std::uint32_t blk_base_ = 0;
    bool epoch_known_ = false;
    std::uint32_t epoch_ = 0;

    os::RawMonotonicClock::time_point now_{};
    bool ready_ = false;
    std::uint32_t round_words_ = 0;
    std::optional<std::uint32_t> queued_{};
    std::optional<os::RawMonotonicClock::time_point> tick_{};
    std::chrono::nanoseconds gap_{};
    WideMovieFrame mf_ = kNoWideMovieFrame;
    WideMovieFrame covered_ = kNoWideMovieFrame;
    WideMovieFrame underrun_at_ = kNoWideMovieFrame;
    std::optional<ReplayMsg::Input> held_{};
    bool end_seen_ = false;
    MovieFrame end_frame_{};
};

}
