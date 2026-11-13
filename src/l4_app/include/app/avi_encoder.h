// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "app/avi_frame_count.h"
#include "app/avi_frame_index.h"
#include "app/chunk_place.h"
#include "app/chunk_slot.h"
#include "app/cscd_codec.h"
#include "app/encode_status.h"
#include "app/frame_arena.h"
#include "app/frame_ticks.h"
#include "app/half_scaler.h"
#include "app/picture_size.h"
#include "app/raw_frame_slot.h"
#include "app/rec_control.h"
#include "app/scale_governor.h"
#include "app/zmbv_codec.h"
#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/types.h"

namespace mister::app {

class AviEncoder {
    TASTY_SEAT_RESIDENT(Encode);

public:
    struct Wiring {

        infra::OptRef<ChunkChannel> out{};

        infra::OptRef<const RawFrameChannel> raw{};
        infra::OptRef<const os::IClock> clock{};
        infra::OptRef<const os::IClock> cpu{};
    };

    struct Limits {
        std::uint64_t segment_bytes = 1ull << 30;
        AviFrameCount segment_frames = kSegmentFrames;
        std::uint32_t key_interval = 300;

        std::chrono::nanoseconds budget_window = ScaleGovernor::Limits{}.budget_window;
        Percent budget_pct = ScaleGovernor::Limits{}.budget_pct;

        std::uint32_t rate_frames = 60;
        std::chrono::nanoseconds recover = ScaleGovernor::Limits{}.recover;
    };

    static constexpr AviFrameCount kSegmentFrames = AviFrameCount{kChunksPerSegment};

    static constexpr std::uint8_t kSustainWindows = ScaleGovernor::kSustainWindows;
    static constexpr std::uint8_t kRecoverDoublings = ScaleGovernor::kRecoverDoublings;

    static constexpr std::int64_t kFlushNs = 250'000'000;
    static constexpr std::size_t kMinSlotBytes = 1024u * 1024u;

    explicit AviEncoder(const Wiring& w) noexcept : w_(w) {}

    AviEncoder(const Wiring& w, const Limits& l) noexcept : w_(w), lim_(l) {}

    AviEncoder(const AviEncoder&) = delete;
    AviEncoder& operator=(const AviEncoder&) = delete;

    [[nodiscard]] bool open(const RawFrameSlot::Open& s, ControlGeneration gen) noexcept;
    [[nodiscard]] bool close(const RawFrameSlot::Close& s, ControlGeneration gen) noexcept;

    [[nodiscard]] bool chunk(
        const RawFrameSlot::Frame& s,
        ControlGeneration gen,
        bool real,
        WideCoreFrame core_frame,
        MovieFrame movie_frame
    ) noexcept;

    [[nodiscard]] bool chunk(
        const RawFrameSlot::Close& s,
        ControlGeneration gen,
        bool real,
        WideCoreFrame core_frame,
        MovieFrame movie_frame
    ) noexcept;

    [[nodiscard]] ChunkPlace place() const noexcept { return place_; }

    void note_held() noexcept { held_ = true; }

    void tend() noexcept;
    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const EncodeStatus::Video& counts() const noexcept { return st_; }

    [[nodiscard]] bool take_dirty() noexcept;

    [[nodiscard]] static RecPath segment_path(const RecPath& sidecar, SegmentIndex index) noexcept;

private:
    [[nodiscard]] bool open_segment_(FrameTicks rate) noexcept;

    template <class A>
    [[nodiscard]] bool lend_(const A& alt) noexcept;
    [[nodiscard]] bool open_marker_(FrameTicks rate) noexcept;
    [[nodiscard]] bool close_marker_() noexcept;
    [[nodiscard]] bool refuse_marker_() noexcept;

    enum class Shortfall : std::uint8_t { Busy, NoMemory };
    [[nodiscard]] std::expected<std::span<std::byte>, Shortfall> reserve_(
        std::size_t payload_max
    ) noexcept;

    [[nodiscard]] std::expected<void, Shortfall> reserve_chunks_(std::size_t need) noexcept;
    void commit_(std::size_t payload, bool key) noexcept;
    void flush_() noexcept;
    [[nodiscard]] bool all_home_() const noexcept;
    [[nodiscard]] bool size_roll_(std::size_t payload_max) const noexcept;

    void note_governor_(const ScaleGovernor::Change& ch) noexcept;
    [[nodiscard]] std::int64_t now_() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds cpu_now_() const noexcept;
    [[nodiscard]] bool kept_(WideCoreFrame core, MovieFrame movie) const noexcept;
    [[nodiscard]] IFrameCodec& codec_for_(RecCodec c) noexcept;
    [[nodiscard]] bool chunk_one_(
        IFrameCodec& codec,
        const RawFrameSlot::Frame& s,
        bool real,
        FrameTicks cand,
        std::uint32_t run,
        bool stable
    ) noexcept;

    [[nodiscard]] PictureSize picture_size_(const RawFrameSlot::Frame& s) const noexcept;

    Wiring w_;
    Limits lim_{};
    CscdCodec cscd_{};
    ZmbvCodec zmbv_{};
    RecOptions opt_{};
    proto::FileSize seg_limit_{};
    IFrameCodec* codec_ = &cscd_;
    RecEvery frame_mul_{};
    HalfScaler half_{};
    FrameArena chunks_{};
    std::size_t data_cap_ = 0;
    EncodeStatus::Video st_{};
    bool dirty_ = false;

    bool active_ = false;
    ControlGeneration gen_{};
    RecPath sidecar_{};
    bool need_roll_ = false;
    bool broken_ = false;

    bool seg_open_ = false;
    SegmentIndex seg_next_{};
    std::uint64_t seg_bytes_ = 0;
    AviFrameCount seg_frames_{};

    FrameTicks seg_rate_{};
    FrameTicks roll_rate_{};
    FrameTicks cand_{};
    std::uint32_t cand_run_ = 0;
    bool need_key_ = true;
    std::uint32_t since_key_ = 0;
    ChunkChannel::Loan loan_{};
    ChunkSlot::Data cur_{};
    std::span<std::byte> data_{};
    std::span<std::byte> desc_{};
    std::int64_t loan_ns_ = 0;
    ChunkPlace place_{};
    bool held_ = false;
    IFrameCodec::Search last_search_{};

    ScaleGovernor gov_{ScaleGovernor::Limits{
        .budget_window = lim_.budget_window,
        .budget_pct = lim_.budget_pct,
        .recover = lim_.recover,
    }};
};

}
