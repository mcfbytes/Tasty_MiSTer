// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/core_window.h"
#include "cores/pcm_feed.h"
#include "cores/pcm_feed_state.h"
#include "cores/pcm_ring.h"
#include "cores/pcm_source.h"
#include "infra/xthread/counter.h"
#include "infra/error.h"
#include "infra/xthread/inbox.h"
#include "infra/xthread/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/xthread/polled.h"
#include "infra/xthread/pool_slot.h"
#include "infra/xthread/release_latch.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "app/path_text.h"
#include "infra/prelude.h"
#include "app/pcm/pcm_command.h"
#include "os/clock.h"
#include "os/map_length.h"
#include "hal/fpga_memory.h"
#include "hal/boards_table.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app::pcm {

inline constexpr std::uint32_t kPcmWatermarkBytes = 1024;
inline constexpr std::uint32_t kPcmChunkBytes = 8192;
inline constexpr std::uint32_t kPcmSampleAlign = 4;

inline constexpr std::chrono::milliseconds kPcmDrainMs{500};

inline constexpr infra::ParkTimeout kPcmIdlePollMs{100};

inline constexpr infra::ParkTimeout kPcmParkedPollMs = infra::kForever;
static_assert(
    kPcmParkedPollMs == infra::kForever,
    "A parked feeder owns no ring and no source, so its main blocks "
    "INDEFINITELY: every waker of one (the stop, request_park, "
    "ready_for_source, set_sheet) is seen by rest() or the stop fd, and a timeout here "
    "would wake an idle FIFO-22 seat ten times a second forever."
);

inline constexpr std::size_t kPcmCommandDepth = 8;
static_assert(
    (kPcmCommandDepth & (kPcmCommandDepth - 1)) == 0,
    "xthread::SpscRing requires a power-of-two capacity"
);

inline constexpr std::size_t kPcmMediaLeases = 4;
static_assert(kPcmMediaLeases < kPcmCommandDepth);

struct PcmMedia {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr bool kResetOnReap = true;
    std::optional<hal::FpgaMemory> ring{};

    PathText sheet{};

    xthread::ReleaseGen stamp{};
};

enum class PcmRelease : std::uint8_t { Source, Media };
using PcmLatch = xthread::BasicReleaseLatch<PcmRelease>;

class PcmRingFeeder final : public cores::ICoreWindow, public cores::IPcmFeed {
    TASTY_SEAT_MEDIATOR(Pcm, RT);

    using Media = xthread::LoanChannel<PcmMedia, kPcmMediaLeases, SeatTag::RT, SeatTag::Pcm>;

public:
    using Commands = xthread::Inbox<PcmCommand, kPcmCommandDepth>;

    PcmRingFeeder(
        const os::IClock& clock,
        Commands& commands,
        xthread::WakeFlag& wake,
        infra::OptRef<const svc::Vfs> vfs
    ) noexcept
        : clock_(clock),
          vfs_(vfs),
          commands_(commands),
          latch_(wake) {}

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

    void on(const PcmCommand::AttachRing& a) noexcept;
    void on(const PcmCommand::AttachSource& a) noexcept;
    void misrouted(const PcmCommand& c) noexcept;

    void fill_pass() noexcept;

    [[nodiscard]] bool rest() const noexcept;
    [[nodiscard]] infra::ParkTimeout rest_ms() const noexcept;

    void release() noexcept;

    void park_now() noexcept;

    void request_park() noexcept;

    [[nodiscard]] bool park_acked() const noexcept { return latch_.released(ticket_); }

    [[nodiscard]] Ex<void> attach(hal::FpgaMemory ring, std::string_view sheet) noexcept;

    [[nodiscard]] Ex<void> adopt_ring(hal::FpgaMemory&& ring) noexcept;

    void detach() noexcept;

    [[nodiscard]] bool parked() const noexcept { return parked_.load(std::memory_order_acquire); }

    [[nodiscard]] bool attached() const noexcept { return rings_handed_ != sample_ended_().rings; }

    [[nodiscard]] Ex<void> play(svc::TrackNumber track, bool loop) override;
    [[nodiscard]] Ex<void> stop() override;
    [[nodiscard]] Ex<void> resume() override;
    void observe_read_point(cores::PcmRingPoint at) noexcept override;
    [[nodiscard]] cores::PcmFeedState state() const noexcept override;
    [[nodiscard]] bool can_serve() const noexcept override;

    [[nodiscard]] bool ready_for_source() noexcept override;

    [[nodiscard]] Ex<void> set_sheet(std::string_view sheet) override;

    [[nodiscard]] Ex<std::size_t> write(std::size_t off, std::span<const std::byte> src) override;
    [[nodiscard]] Ex<std::size_t> read(std::size_t off, std::span<std::byte> dst) const override;
    void publish() noexcept override;
    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] infra::OptRef<cores::IPcmFeed> pcm_feed() noexcept override;

    [[nodiscard]] std::uint32_t wakes() const noexcept { return wakes_.get(); }

    [[nodiscard]] std::uint32_t command_drops() const noexcept { return drops_.get(); }

    [[nodiscard]] std::uint32_t command_discards() const noexcept { return discards_.get(); }

    [[nodiscard]] std::uint32_t fill_passes() const noexcept { return passes_.get(); }

    [[nodiscard]] std::uint32_t command_misrouted() const noexcept { return misrouted_.get(); }

    [[nodiscard]] Media::Census media_census_for_test() const noexcept { return media_.census(); }

    [[nodiscard]] bool returned_media_hold_no_ring_for_test() noexcept;

private:
    void wake_main_() noexcept;
    [[nodiscard]] Ex<void> submit_(const PcmCommand& c) noexcept;
    void request_swap_() noexcept;

    [[nodiscard]] bool reap_media_() noexcept;

    template <class Alt>
    [[nodiscard]] Ex<void> hand_over_(PcmMedia& media) noexcept;

    [[nodiscard]] std::optional<PcmMedia> take_media_(xthread::PoolSlot announced) noexcept;

    static constexpr std::uint32_t kPrefillParts = 2;
    void refresh_read_point_() noexcept;
    void apply_play_(svc::TrackNumber track, bool loop) noexcept;
    void fill_() noexcept;
    void release_media_() noexcept;
    void drop_ring_() noexcept;
    void release_source_() noexcept;
    void reset_playback_() noexcept;
    void publish_state_() noexcept;
    [[nodiscard]] std::uint32_t prefill_target_() const noexcept;

    [[nodiscard]] std::size_t write_ring_(std::size_t n) noexcept;

    struct Ended {
        cores::PcmHandovers rings{};
        cores::PcmHandovers sources{};
    };

    [[nodiscard]] Ended sample_ended_() const noexcept;
    [[nodiscard]] bool source_live_() const noexcept;

    const os::IClock& clock_;

    infra::OptRef<const svc::Vfs> vfs_{};

    Commands& commands_;
    xthread::Telemetry<cores::PcmRingPoint, SeatTag::RT> read_point_{};
    xthread::Telemetry<cores::PcmFeedState, SeatTag::Pcm> state_{};

    PcmLatch latch_;

    xthread::Counter wakes_{}, drops_{}, discards_{}, passes_{}, misrouted_{}, refused_markers_{};

    Media media_{xthread::Polled{}};

    cores::PcmHandovers rings_handed_{};
    cores::PcmHandovers sources_handed_{};
    os::MapLength handed_len_{};
    mutable Ended ended_{};

    PcmLatch::Ticket ticket_{};

    cores::PcmRingPoint last_observed_{};
    bool ever_observed_ = false;

    std::atomic<bool> parked_{true};
    std::optional<hal::FpgaMemory> ring_{};

    xthread::ReleaseGen ring_stamp_{};
    std::unique_ptr<cores::IPcmSource> src_{};
    cores::PcmRing ring_geom_{};
    cores::PcmRingPoint wr_{};
    proto::FileSize position_{};
    cores::PcmExtent extent_{};
    bool playing_ = false;
    bool paused_ = false;
    bool draining_ = false;
    bool primed_ = false;
    bool drained_ = false;
    std::uint32_t served_ = 0;

    cores::PcmHandovers rings_ended_{};
    cores::PcmHandovers sources_ended_{};

    struct Worker {
        TASTY_SEAT_RESIDENT(Pcm);
        std::optional<PcmLatch::Taken> taken{};
    };

    Worker work_{};

    infra::CellGen rp_seen_{};
    cores::PcmRingPoint read_val_{};
    os::RawMonotonicClock::time_point drain_until_{};
    cores::PcmFeedState published_{};

    std::byte staging_[kPcmChunkBytes]{};
};

static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, SeatTag::Pcm).spawn == hal::SpawnKind::Create;
    }),
    "T-PCM is spawned, not promoted in place: PcmMain::start is a "
    "thread body reached through ThreadAssembly::trampoline<PcmMain>, "
    "the same shape as RtMain::start behind trampoline<RtMain>."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, SeatTag::Pcm).policy == hal::SchedPolicy::Fifo
            && hal::seat_of(m, SeatTag::Pcm).prio < hal::seat_of(m, SeatTag::RT).prio;
    }),
    "This seat blocks on poll(2) and on file reads, "
    "and it must never be able to preempt the sole GPO owner."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, SeatTag::Pcm).cpu != hal::seat_of(m, SeatTag::RT).cpu;
    }),
    "thread_map.h's rt_cpu_reserved, restated where the worker is: up "
    "to 8 KiB of memcpy into uncached FPGA DDR per pass must not "
    "contend for the CPU the executive's 5 ms cadence is measured "
    "on."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, SeatTag::Pcm).stack_bytes == hal::kFifoStackBytes;
    }),
    "the staging buffer is a MEMBER, not a frame: the row's stack is "
    "the FIFO stack every created RT seat takes, unchanged."
);

}
