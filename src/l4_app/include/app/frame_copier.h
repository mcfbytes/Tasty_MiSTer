// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "app/avi_write_status.h"
#include "app/core_frame_record.h"
#include "app/frame_arena.h"
#include "app/frame_demand.h"
#include "app/frame_numbering.h"
#include "app/frame_stamp.h"
#include "app/raw_frame_slot.h"
#include "app/rec_control.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "app/types.h"
#include "app/video_geometry_record.h"
#include "hal/scaler_buffers.h"
#include "hal/scaler_header.h"
#include "hal/scaler_probe.h"
#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/delay.h"

namespace mister::app {

class FrameCopier {
    TASTY_SEAT_RESIDENT(Capture);

public:
    struct Wiring {
        const os::IClock& clock;

        os::IDelay& delay;
        RecControlChannel& control;
        RecorderStatusCell& status;
        FrameDemandCell& demand;
        const CoreFrameCell& frames;
        const ReplayStatusCell& replay;
        const RecWriteStatusCell& writer;
        const AviWriteStatusCell& avi_writer;
        const VideoGeometryCell& video;

        RawFrameChannel& channel;
    };

    static constexpr infra::ParkTimeout kPollMs{4};

    static constexpr std::int64_t kParkPeriods = 12;

    static constexpr CoreFrameCount kReplayTailFrames{60};

    static constexpr std::int64_t kEpochWaitNs = 90'000'000'000;

    FrameCopier(const Wiring& w, std::optional<hal::ScalerBuffers> window) noexcept
        : w_(w),
          window_(std::move(window)),
          requests_(w_.control.worker()) {}

    FrameCopier(const FrameCopier&) = delete;
    FrameCopier& operator=(const FrameCopier&) = delete;

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const RecorderStatus& status() const noexcept { return st_; }

    using BufMask = hal::ScalerProbe::BufMask;
    using Pick = hal::ScalerProbe::Pick;

private:
    using Chan = RecControlChannel;

    void drain_control_() noexcept;
    void take_request_(Chan::Job job) noexcept;
    void answer_(Chan::Job& job, RecVerdict v) noexcept;

    void answer_pending_(RecVerdict v) noexcept;
    void begin_(Chan::Job& job) noexcept;
    void end_(RecVerdict why) noexcept;
    void set_demand_(bool on) noexcept;
    void reap_() noexcept;
    [[nodiscard]] bool all_home_() const noexcept;

    void probe_step_(std::int64_t now) noexcept;
    [[nodiscard]] bool open_segment_() noexcept;
    void close_step_() noexcept;
    void watch_step_() noexcept;

    void note_replay_(const ReplayStatus& r) noexcept;
    [[nodiscard]] bool replay_before_epoch_() const noexcept;

    [[nodiscard]] std::optional<std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers>>
    read_heads_() const noexcept;

    [[nodiscard]] std::size_t base_(std::size_t buf) const noexcept {
        return buf * hal::stride_bytes(gen_.stride);
    }

    void poll_triple_(std::int64_t now) noexcept;

    void track_(
        const std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers>& hd,
        std::int64_t now
    ) noexcept;
    void track_reset_(const BufMask& live, std::int64_t now) noexcept;
    void poll_lowlat_(std::int64_t now) noexcept;

    [[nodiscard]] std::int64_t approach_(std::int64_t now) noexcept;

    void follow_mode_(bool single, std::int64_t now) noexcept;

    void on_complete_(
        std::size_t buf,
        hal::HeaderCtr ctr,
        std::int64_t now,
        CoreFrameGap behind,
        std::uint32_t fields
    ) noexcept;

    [[nodiscard]] bool open_armed_(
        const FrameStamp& s,
        infra::OptRef<const CoreFrameRecord> cell,
        std::uint32_t fields,
        std::int64_t now
    ) noexcept;

    [[nodiscard]] CoreFrameGap lag_(CoreFrameGap behind) const noexcept {
        return gen_.lowlat ? CoreFrameGap{} : CoreFrameGap::of_lead(gen_.lag_steps) + behind;
    }

    void attach_pending_(RawFrameSlot::Close& slot) noexcept;
    void defer_(const FrameStampRun& run) noexcept;
    [[nodiscard]] bool geometry_ok_(const hal::ScalerHeader& h) const noexcept;

    [[nodiscard]] bool rate_is_ours_(std::uint32_t seq) const noexcept;
    void deliver_(std::size_t buf, const hal::ScalerHeader& h, const FrameStamp& s) noexcept;
    [[nodiscard]] bool regrow_() noexcept;
    [[nodiscard]] bool reserve_arena_(std::size_t frame_bytes) noexcept;

    Wiring w_;
    std::optional<hal::ScalerBuffers> window_;
    Chan::Worker requests_{};
    std::optional<Chan::Job> answering_{};
    RecorderStatus st_{};
    bool dirty_ = false;
    bool demand_on_ = false;
    FrameArena arena_{};

    struct RecGeneration {
        RecPath path{};
        RecMode mode = RecMode::Hash;
        RecOptions opt{};
        bool from_arm = false;
        bool open_sent = false;
        bool close_sent = false;
        hal::ScalerStride stride = hal::ScalerStride::Large;
        bool lowlat = false;
        std::size_t regrow_bytes = 0;

        hal::HeaderCtr::Lag lag_steps{1};
        bool woven = false;
        bool matched = true;
        std::uint8_t npending = 0;
        std::optional<CoreFrameRecord> last_cell{};
        bool core_seq_known = false;

        bool ll_waiting = false;
        std::int64_t ll_push_ns = 0;

        bool replay_running = false;
        bool covers_zero = false;
        std::int64_t epoch_wait_ns = -1;
        CoreFrameCount tail_left{};
        bool tail_counting = false;
    };

    RecGeneration gen_{};

    hal::ScalerProbe probe_{};

    struct BufferWatch {
        hal::ScalerHeader last{};
        std::chrono::nanoseconds seen{};
    };

    std::array<BufferWatch, hal::ScalerBuffers::kBuffers> watch_{};
    std::int64_t track_ns_ = 0;
    BufMask live_{};

    FrameNumbering numbering_{};
    std::uint32_t core_seq_ = 0;
    std::array<FrameStampRun, RawFrameSlot::kMaxRuns> pending_{};

    hal::HeaderCtr ll_ctr_{};
    std::int64_t ll_due_ns_ = 0;
    std::int64_t copy_ns_ = 1'000'000;

    std::int64_t armed_ns_ = 0;
};

}
