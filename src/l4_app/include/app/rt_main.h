// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/replay_msg.h"
#include "app/rt_park.h"
#include "hal/link_timing.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_main.h"

namespace mister::reactor {
class Executive;
class RoundTimer;
}  // namespace mister::reactor

namespace mister::app {

class LinkSession;
class InputEmit;
class InputWire;
class LinkTxChannel;
class OsdWire;
class ReplayGate;
class CoreFrameCounter;
class ScanoutRelay;

class RtMain final : public xthread::SeatMain<RtMain> {
    TASTY_SEAT_RESIDENT(RT);

public:
    RtMain(reactor::Executive& exec, LinkSession* session, InputEmit* in,
           const hal::LinkTimingValues& timing, OsdWire* osd = nullptr,
           reactor::RoundTimer* timer = nullptr, InputWire* wire = nullptr) noexcept
        : exec_(exec), session_(session), input_(in), osd_(osd), timer_(timer), wire_(wire),
          park_(exec), round_budget_words_(timing.round_budget_words) {}
    RtMain(const RtMain&) = delete;
    RtMain& operator=(const RtMain&) = delete;
    RtMain(RtMain&&) = delete;
    RtMain& operator=(RtMain&&) = delete;

    void bind_replay(ReplayGate* gate, ReplayRing* ring) noexcept {
        replay_ = (gate != nullptr && ring != nullptr) ? gate : nullptr;
        replay_ring_ = ring;
    }

    void bind_frames(CoreFrameCounter* frames) noexcept { frames_ = frames; }

    void bind_scanout(ScanoutRelay* relay) noexcept { scanout_ = relay; }

    void start() noexcept;

    void stop() noexcept;

    void round(bool tick);

    void drain_ops() noexcept;

    [[nodiscard]] Ex<void> pump_boot();

    [[nodiscard]] Ex<void> result() const noexcept;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept { return true; }
    [[nodiscard]] bool stopping() const noexcept { return stopping_; }
    [[nodiscard]] int park_ms() const noexcept { return -1; }
    [[nodiscard]] RtPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept;

    static constexpr unsigned kLinkOpBudget = 8;
    static constexpr unsigned kInputOpBudget = 2 * 6;

private:
    unsigned drain_(LinkTxChannel& inbox, unsigned already, unsigned budget,
                    bool windows_closed) noexcept;

    bool drain_fio_() noexcept;

    void replay_step_() noexcept;

    void frames_step_() noexcept;

    void scanout_step_() noexcept;
    [[nodiscard]] bool replay_ready_() const noexcept;
    [[nodiscard]] bool switch_window_() const noexcept;
    void note_switch_() const noexcept;
    [[nodiscard]] std::uint32_t core_edge_seq_() const noexcept;
    [[nodiscard]] bool session_live_() const noexcept;
    [[nodiscard]] bool wire_held_() const noexcept;

    reactor::Executive& exec_;
    LinkSession* session_;

    InputEmit* input_;
    OsdWire* osd_;

    reactor::RoundTimer* timer_;

    InputWire* wire_;
    RtPark park_;
    ReplayGate* replay_ = nullptr;
    ReplayRing* replay_ring_ = nullptr;
    CoreFrameCounter* frames_ = nullptr;
    ScanoutRelay* scanout_ = nullptr;
    bool stopping_ = false;
    Ex<void> result_{};
    std::uint32_t round_budget_words_;
};

static_assert(xthread::SeatBody<RtMain>);

}  // namespace mister::app
