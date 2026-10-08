// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "app/avi_write_status.h"
#include "app/core_frame_record.h"
#include "app/frame_arena.h"
#include "app/frame_demand.h"
#include "app/frame_stamp.h"
#include "app/raw_frame_slot.h"
#include "app/rec_control.h"
#include "app/rec_write_status.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "app/video_pump.h"
#include "hal/scaler_buffers.h"
#include "hal/scaler_header.h"
#include "hal/scaler_probe.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/delay.h"

namespace mister::app {

class FrameCopier {
    TASTY_SEAT_RESIDENT(Capture);

public:
    struct Wiring {
        const os::IClock* clock = nullptr;

        os::IDelay* delay = nullptr;
        const RecControlCell* control = nullptr;
        RecorderStatusCell* status = nullptr;
        FrameDemandCell* demand = nullptr;
        const CoreFrameCell* frames = nullptr;
        const ReplayStatusCell* replay = nullptr;
        const RecWriteStatusCell* writer = nullptr;
        const AviWriteStatusCell* avi_writer = nullptr;
        const VideoGeometryCell* video = nullptr;
        RawFrameChannel* channel = nullptr;
    };

    static constexpr int kPollMs = 4;
    static constexpr std::int64_t kDefaultPeriodNs = 16'666'667;

    static constexpr std::int64_t kParkPeriods = 12;

    static constexpr std::uint32_t kReplayTailFrames = 60;

    static constexpr std::int64_t kEpochWaitNs = 90'000'000'000;

    FrameCopier(const Wiring& w, std::optional<hal::ScalerBuffers> window) noexcept
        : w_(w), window_(std::move(window)) {}
    FrameCopier(const FrameCopier&) = delete;
    FrameCopier& operator=(const FrameCopier&) = delete;

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const RecorderStatus& status() const noexcept { return st_; }

    using BufMask = hal::ScalerProbe::BufMask;
    using Pick = hal::ScalerProbe::Pick;

private:
    struct Heads {
        std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers> h{};
        bool ok = false;
    };
    void sample_control_() noexcept;
    void answer_(std::uint16_t gen, RecVerdict v) noexcept;
    void begin_(const RecControl& c) noexcept;
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

    [[nodiscard]] Heads read_heads_() const noexcept;
    [[nodiscard]] std::size_t base_(std::size_t buf) const noexcept { return buf * stride_; }
    void poll_triple_(std::int64_t now) noexcept;

    void track_(const Heads& hd, std::int64_t now) noexcept;
    void track_reset_(const BufMask& live, std::int64_t now) noexcept;
    void poll_lowlat_(std::int64_t now) noexcept;

    [[nodiscard]] std::int64_t approach_(std::int64_t now) noexcept;

    void follow_mode_(bool single, std::int64_t now) noexcept;

    void on_complete_(std::size_t buf, std::uint8_t ctr, std::int64_t now, std::int64_t behind,
                      std::uint32_t fields) noexcept;

    [[nodiscard]] bool open_armed_(const FrameStamp& s, const CoreFrameRecord* cell,
                                   std::uint32_t fields, std::int64_t now) noexcept;

    [[nodiscard]] std::uint32_t frames_since_(std::uint8_t ctr, std::int64_t now,
                                              const CoreFrameRecord* cell,
                                              std::int64_t behind) noexcept;
    [[nodiscard]] FrameStamp stamp_(std::uint8_t ctr, std::int64_t now, const CoreFrameRecord* cell,
                                    std::int64_t lag) noexcept;

    void anchor_(const CoreFrameRecord& cell, std::int64_t lag, bool vote) noexcept;

    [[nodiscard]] std::int64_t lag_(std::int64_t behind) const noexcept {
        return lowlat_ ? 0 : lag_steps_ + behind;
    }
    void rebase_(const CoreFrameRecord& cell, std::uint64_t core, std::int64_t lag) noexcept;
    void cross_check_(const CoreFrameRecord& cell, std::uint64_t core, std::int64_t lag) noexcept;
    [[nodiscard]] std::int32_t movie_of_(const CoreFrameRecord& cell,
                                         std::uint64_t core) const noexcept;
    void attach_pending_(RawFrameSlot& slot) noexcept;
    void defer_(std::uint64_t first, std::uint32_t count, std::uint8_t first_ctr,
                DupReason why) noexcept;
    [[nodiscard]] bool geometry_ok_(const hal::ScalerHeader& h) const noexcept;

    [[nodiscard]] bool rate_is_ours_(std::uint32_t seq) const noexcept;
    void deliver_(std::size_t buf, const hal::ScalerHeader& h, const FrameStamp& s) noexcept;
    [[nodiscard]] bool regrow_() noexcept;
    [[nodiscard]] bool reserve_arena_(std::size_t slot_bytes) noexcept;

    Wiring w_;
    std::optional<hal::ScalerBuffers> window_;
    RecControlCell::Reader control_seen_{};
    RecorderStatus st_{};
    bool dirty_ = false;
    bool demand_on_ = false;
    FrameArena arena_{};

    RecPath path_{};
    RecMode mode_ = RecMode::Hash;
    RecOptions opt_{};
    bool from_arm_ = false;
    bool open_sent_ = false;
    bool close_sent_ = false;
    std::size_t stride_ = 0;
    bool lowlat_ = false;
    std::size_t regrow_bytes_ = 0;

    hal::ScalerProbe probe_{};
    std::int64_t period_ns_ = kDefaultPeriodNs;

    std::array<std::array<std::uint16_t, 8>, hal::ScalerBuffers::kBuffers> seen_key_{};
    std::array<std::int64_t, hal::ScalerBuffers::kBuffers> seen_ns_{};
    std::int64_t track_ns_ = 0;
    BufMask live_{};

    struct LastCtr {
        std::uint8_t ctr = 0;
        std::int64_t seen_ns = 0;
    };
    std::optional<LastCtr> last_{};
    std::uint64_t ext_ = 0;
    std::int64_t anchor_off_ = 0;

    static constexpr std::size_t kAnchorVotes = 8;
    std::array<std::int64_t, kAnchorVotes> votes_{};
    std::uint8_t nvotes_ = 0;
    std::uint8_t vote_next_ = 0;

    std::int64_t cell_off_ = 0;
    std::uint32_t cell_epoch_ = 0;
    bool core_seq_known_ = false;
    std::uint32_t core_seq_ = 0;
    std::array<FrameStampRun, RawFrameSlot::kMaxRuns> pending_{};

    std::int64_t lag_steps_ = 1;
    bool woven_ = false;
    bool matched_ = true;
    std::uint8_t npending_ = 0;
    CoreFrameRecord last_cell_{};
    bool have_cell_ = false;

    bool ll_waiting_ = false;
    std::uint8_t ll_ctr_ = 0;
    std::int64_t ll_due_ns_ = 0;
    std::int64_t ll_push_ns_ = 0;
    std::int64_t copy_ns_ = 1'000'000;

    bool replay_running_ = false;
    bool covers_zero_ = false;
    std::int64_t epoch_wait_ns_ = -1;
    std::int64_t armed_ns_ = 0;
    std::uint32_t tail_left_ = 0;
    bool tail_counting_ = false;
};

}  // namespace mister::app
