// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "app/replay_ring.h"
#include "app/rt_park.h"
#include "app/types.h"
#include "hal/link_timing.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "proto/types.h"

namespace mister::reactor {
class Executive;
class RoundTimer;
}

namespace mister::app {

class LinkSession;
class InputEmit;
class InputWire;
template <SeatTag Producer>
class LinkTxChannel;
class OsdWire;
class ReplayGate;
class CoreFrameCounter;
class ScanoutRelay;

class RtMain final : public xthread::SeatMain<RtMain> {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        infra::OptRef<LinkSession> session{};
        infra::OptRef<InputEmit> input{};
        infra::OptRef<OsdWire> osd{};
        infra::OptRef<reactor::RoundTimer> timer{};
        infra::OptRef<InputWire> wire{};
        infra::OptRef<CoreFrameCounter> frames{};
        infra::OptRef<ScanoutRelay> scanout{};
        infra::OptRef<ReplayGate> replay{};
        ReplayRing::Receiver replay_ring{};
    };

    RtMain(reactor::Executive& exec, const hal::LinkTimingValues& timing, Wiring wiring) noexcept
        : exec_(exec),
          session_(wiring.session),
          input_(wiring.input),
          osd_(wiring.osd),
          timer_(wiring.timer),
          wire_(wiring.wire),
          park_(exec),
          replay_(
              (wiring.replay && wiring.replay_ring) ? wiring.replay : infra::OptRef<ReplayGate>{}
          ),
          replay_ring_(std::move(wiring.replay_ring)),
          frames_(wiring.frames),
          scanout_(wiring.scanout),
          round_budget_words_(proto::FioWordCount{timing.round_budget_words}) {}

    RtMain(const RtMain&) = delete;
    RtMain& operator=(const RtMain&) = delete;
    RtMain(RtMain&&) = delete;
    RtMain& operator=(RtMain&&) = delete;

    void start() noexcept;

    void stop() noexcept;

    void round(bool tick);

    void drain_ops() noexcept;

    [[nodiscard]] Ex<void> pump_boot();

    [[nodiscard]] Ex<void> result() const noexcept;

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept { return true; }

    [[nodiscard]] bool stopping() const noexcept { return stopping_; }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept { return infra::kForever; }

    [[nodiscard]] RtPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }

    [[nodiscard]] bool pause_pending() const noexcept { return false; }

    void settle() noexcept;

    static constexpr unsigned kLinkOpBudget = 8;
    static constexpr unsigned kInputOpBudget = 2 * 6;

private:
    class FioRoundBudget final {
    public:
        explicit constexpr FioRoundBudget(proto::FioWordCount whole) noexcept : left_(whole) {}

        constexpr void spend(proto::FioWordCount words) noexcept {
            left_ = left_.saturating_sub(words);
        }

        [[nodiscard]] constexpr proto::FioWordCount remaining() const noexcept { return left_; }

    private:
        proto::FioWordCount left_;
    };

    class OpRoundBudget final {
    public:
        explicit constexpr OpRoundBudget(unsigned ops) noexcept : ops_left_(ops) {}

        [[nodiscard]] constexpr bool exhausted() const noexcept { return ops_left_ == 0; }

        constexpr void take() noexcept { --ops_left_; }

    private:
        unsigned ops_left_;
    };

    template <SeatTag Producer>
    void drain_(
        LinkTxChannel<Producer>& inbox,
        OpRoundBudget& budget,
        bool windows_closed
    ) noexcept;

    bool drain_fio_() noexcept;

    void replay_step_() noexcept;

    void frames_step_() noexcept;

    void scanout_step_() noexcept;
    [[nodiscard]] bool replay_ready_() const noexcept;
    [[nodiscard]] bool switch_window_() const noexcept;
    void note_switch_() const noexcept;
    [[nodiscard]] CoreEdgeSeq core_edge_seq_() const noexcept;
    [[nodiscard]] bool session_live_() const noexcept;
    [[nodiscard]] bool wire_held_() const noexcept;

    [[nodiscard]] bool transfer_holds_() const noexcept;

    reactor::Executive& exec_;
    infra::OptRef<LinkSession> session_{};

    infra::OptRef<InputEmit> input_{};
    infra::OptRef<OsdWire> osd_{};

    infra::OptRef<reactor::RoundTimer> timer_{};

    infra::OptRef<InputWire> wire_{};
    RtPark park_;
    infra::OptRef<ReplayGate> replay_{};
    ReplayRing::Receiver replay_ring_{};
    infra::OptRef<CoreFrameCounter> frames_{};
    infra::OptRef<ScanoutRelay> scanout_{};
    bool stopping_ = false;
    Ex<void> result_{};
    proto::FioWordCount round_budget_words_;
};

static_assert(xthread::SeatBody<RtMain>);

}
