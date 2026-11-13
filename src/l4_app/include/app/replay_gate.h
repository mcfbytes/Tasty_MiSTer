// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "app/core_frame_counter.h"
#include "app/epoch_rule.h"
#include "app/frame_reference.h"
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

    static constexpr CoreFrameCount kAliasFrames{200};

    static constexpr std::chrono::nanoseconds kTickNs{reactor::kTickNs};

    static constexpr CoreFrameCount kFloorBack{1};

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
    struct ArmFacts {
        svc::PlayerMask ports{};
        proto::IoIndex rom = kNoRomSlot;
        bool vsync_ok = false;
        MovieSpan lead{};
        std::chrono::nanoseconds offset{};
        std::chrono::nanoseconds tick{};
        EpochRule epoch{};
        ReplayMsg::PowerOnEvent event = ReplayMsg::PowerOnEvent::LoadEnd;
        CoreFrameCount floor_back{};
        WideMovieFrame pre_epoch_below{};
        std::chrono::nanoseconds alias{};
        FrameReference::Dating dating{};

        [[nodiscard]] static std::optional<ArmFacts> of(const ReplayMsg::Arm& a) noexcept;
        [[nodiscard]] bool admits(proto::IoIndex wire) const noexcept;
        [[nodiscard]] bool untimed(const EpochRule::Pick& p) const noexcept;
    };

    struct Run {
        FrameReference ref{};
        FrameReference::Instant began{};
        std::optional<CoreFrameCount> epoch{};
        std::uint32_t blk_base = 0;
        std::optional<FrameReference::Instant> tick{};
        std::chrono::nanoseconds gap{};
        WideMovieFrame mf = kNoWideMovieFrame;
        WideMovieFrame covered = kNoWideMovieFrame;
        WideMovieFrame underrun_at = kNoWideMovieFrame;
        std::optional<ReplayMsg::Input> held{};
        std::optional<MovieFrame> end{};
    };

    void sample_control_() noexcept;
    [[nodiscard]] bool sample_ref_() noexcept;
    void begin_run_() noexcept;
    void decide_epoch_(FrameReference::Step step) noexcept;
    void decide_silence_(FrameReference::Step step) noexcept;

    [[nodiscard]] infra::OptRef<const FrameReference::Dating> dating_() const noexcept;
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

    [[nodiscard]] static Wrapping wrap_(proto::FrameCount c) noexcept;

    Wiring w_;

    proto::SpiFrame frame_{w_.link};
    ReplayControlCell::Reader control_seen_{};
    ReplayControl ctrl_{};
    ReplayStatus status_{};
    bool dirty_ = false;

    ReplayLevel level_ = ReplayLevel::Idle;
    ControlGeneration gen_{};

    svc::PlayerMask release_ports_{};
    CoreEdgeSeq edge_seq_{};

    ArmFacts facts_{};

    Run run_{};
    std::uint32_t blk_seen_ = 0;

    os::RawMonotonicClock::time_point now_{};
    bool ready_ = false;
    std::uint32_t round_words_ = 0;
    std::optional<std::uint32_t> queued_{};
};

}
