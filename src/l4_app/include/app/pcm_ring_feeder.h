// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/core_window.h"
#include "cores/pcm_feed.h"
#include "cores/pcm_feed_state.h"
#include "cores/pcm_source.h"
#include "infra/error.h"
#include "infra/inbox.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "app/pcm_command.h"
#include "os/clock.h"
#include "hal/fpga_memory.h"
#include "hal/boards_table.h"

namespace mister::app {

inline constexpr std::uint32_t kPcmWatermarkBytes = 1024;
inline constexpr std::uint32_t kPcmChunkBytes = 8192;
inline constexpr std::uint32_t kPcmSampleAlign = 4;

inline constexpr std::chrono::milliseconds kPcmDrainMs{500};

inline constexpr int kPcmIdlePollMs = 100;

inline constexpr int kPcmParkedPollMs = -1;
static_assert(kPcmParkedPollMs < 0,
              "A parked feeder owns no ring and no source, so its main blocks "
              "INDEFINITELY: every waker of one (the stop, request_park, "
              "ready_for_source, set_source) is seen by rest() or the stop fd, and a timeout here "
              "would wake an idle FIFO-22 seat ten times a second forever.");

inline constexpr std::size_t kPcmCommandDepth = 8;
static_assert((kPcmCommandDepth & (kPcmCommandDepth - 1)) == 0,
              "xthread::SpscRing requires a power-of-two capacity");

class PcmRingFeeder final : public cores::ICoreWindow, public cores::IPcmFeed {
    TASTY_SEAT_MEDIATOR(Pcm, RT);

public:
    using Commands = xthread::Inbox<PcmCommand, kPcmCommandDepth>;

    PcmRingFeeder(const os::IClock& clock, Commands& commands) noexcept
        : clock_(&clock), commands_(&commands) {}
    ~PcmRingFeeder() override = default;

    PcmRingFeeder(const PcmRingFeeder&) = delete;
    PcmRingFeeder& operator=(const PcmRingFeeder&) = delete;
    PcmRingFeeder(PcmRingFeeder&&) = delete;
    PcmRingFeeder& operator=(PcmRingFeeder&&) = delete;

    [[nodiscard]] bool take_park_ask() noexcept;
    void finish_park() noexcept;

    void on(const PcmCommand::Play& a) noexcept;
    void on(const PcmCommand::Stop& a) noexcept;
    void on(const PcmCommand::Resume& a) noexcept;
    void misrouted(const PcmCommand& c) noexcept;

    void fill_pass() noexcept;

    [[nodiscard]] bool rest() const noexcept;
    [[nodiscard]] int rest_ms() const noexcept;

    void release() noexcept;

    void park_now() noexcept;

    void request_park() noexcept;

    [[nodiscard]] Ex<void> attach(hal::FpgaMemory ring,
                                  std::unique_ptr<cores::IPcmSource> src) noexcept;

    [[nodiscard]] Ex<void> attach_ring(hal::FpgaMemory ring) noexcept;

    void detach() noexcept;

    [[nodiscard]] bool parked() const noexcept { return parked_.load(std::memory_order_acquire); }

    [[nodiscard]] bool attached() const noexcept {
        return visible_len_.load(std::memory_order_acquire) != 0;
    }

    [[nodiscard]] Ex<void> play(std::uint8_t track, bool loop) override;
    [[nodiscard]] Ex<void> stop() override;
    [[nodiscard]] Ex<void> resume() override;
    void observe_read_point(std::uint32_t off) noexcept override;
    [[nodiscard]] cores::PcmFeedState state() const noexcept override;
    [[nodiscard]] bool can_serve() const noexcept override;

    [[nodiscard]] bool ready_for_source() noexcept override;

    [[nodiscard]] Ex<void> set_source(std::unique_ptr<cores::IPcmSource> src) override;

    [[nodiscard]] Ex<std::size_t> write(std::size_t off, std::span<const std::byte> src) override;
    [[nodiscard]] Ex<std::size_t> read(std::size_t off, std::span<std::byte> dst) const override;
    void publish() noexcept override;
    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] cores::IPcmFeed* pcm_feed() noexcept override;

    [[nodiscard]] std::uint32_t wakes() const noexcept {
        return wakes_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t command_drops() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t command_discards() const noexcept {
        return discards_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t fill_passes() const noexcept {
        return passes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t command_misrouted() const noexcept {
        return misrouted_.load(std::memory_order_relaxed);
    }

private:
    void wake_main_() noexcept;
    [[nodiscard]] Ex<void> submit_(const PcmCommand& c) noexcept;
    void request_swap_() noexcept;

    void refresh_read_point_() noexcept;
    void apply_play_(std::uint8_t track, bool loop) noexcept;
    void fill_() noexcept;
    void release_media_() noexcept;
    void release_source_() noexcept;
    void reset_playback_() noexcept;
    void publish_state_() noexcept;
    [[nodiscard]] std::uint32_t prefill_target_() const noexcept;

    [[nodiscard]] std::size_t write_ring_(std::size_t n) noexcept;

    const os::IClock* clock_;

    Commands* const commands_;
    xthread::Telemetry<std::uint32_t, SeatTag::RT> read_point_{};
    xthread::Telemetry<cores::PcmFeedState, SeatTag::Pcm> state_{};

    enum class Park : std::uint8_t {
        Idle = 0,
        Owed = 1,
        Releasing = 2,
        SwapOwed = 3,
        SwapReleasing = 4,
    };
    std::atomic<Park> park_{Park::Idle};
    std::atomic<bool> parked_{true};

    std::atomic<std::uint32_t> wakes_{0}, drops_{0}, discards_{0}, passes_{0}, misrouted_{0};

    std::atomic<std::uint32_t> visible_len_{0};

    std::uint32_t last_observed_ = 0;
    bool ever_observed_ = false;

    std::optional<hal::FpgaMemory> ring_{};
    std::unique_ptr<cores::IPcmSource> src_{};
    std::uint32_t ring_len_ = 0;
    std::uint32_t ring_mask_ = 0;
    std::uint32_t wr_ = 0;
    std::uint64_t position_ = 0;
    cores::PcmExtent extent_{};
    bool playing_ = false;
    bool paused_ = false;
    bool draining_ = false;
    bool primed_ = false;
    bool drained_ = false;
    std::uint32_t served_ = 0;

    std::uint32_t rp_seen_ = 0;
    std::uint32_t read_val_ = 0;
    std::chrono::nanoseconds drain_until_{0};
    cores::PcmFeedState published_{};

    std::byte staging_[kPcmChunkBytes]{};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, SeatTag::Pcm).spawn == hal::SpawnKind::Create;
              }),
              "T-PCM is spawned, not promoted in place: PcmMain::start is a "
              "thread body reached through ThreadAssembly::trampoline<PcmMain>, "
              "the same shape as RtMain::start behind trampoline<RtMain>.");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, SeatTag::Pcm).policy == hal::SchedPolicy::Fifo &&
                         hal::seat_of(m, SeatTag::Pcm).prio < hal::seat_of(m, SeatTag::RT).prio;
              }),
              "arch RULE 2: this seat blocks on poll(2) and on file reads, "
              "and it must never be able to preempt the sole GPO owner.");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, SeatTag::Pcm).cpu != hal::seat_of(m, SeatTag::RT).cpu;
              }),
              "thread_map.h's pcm_off_rt_cpu, restated where the worker is: up "
              "to 8 KiB of memcpy into uncached FPGA DDR per pass must not "
              "contend for the CPU the executive's 5 ms cadence is measured "
              "on.");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, SeatTag::Pcm).stack_bytes == hal::kFifoStackBytes;
              }),
              "the staging buffer is a MEMBER, not a frame: the row's stack is "
              "the FIFO stack every created RT seat takes, unchanged.");

}  // namespace mister::app
