// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/chunk_slot.h"
#include "app/cscd_codec.h"
#include "app/encode_status.h"
#include "app/frame_arena.h"
#include "app/raw_frame_slot.h"
#include "app/rec_control.h"
#include "app/zmbv_codec.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class AviEncoder {
    TASTY_SEAT_RESIDENT(Encode);

public:
    struct Wiring {
        ChunkChannel* out = nullptr;
        const RawFrameChannel* raw = nullptr;
        const os::IClock* clock = nullptr;
        const os::IClock* cpu = nullptr;
    };

    struct Limits {
        std::uint64_t segment_bytes = 1ull << 30;
        std::uint32_t segment_frames = kSegmentFrames;
        std::uint32_t key_interval = 300;
        std::int64_t budget_window_ns = 2'000'000'000;
        std::uint32_t budget_pct = 50;

        std::uint32_t rate_frames = 60;

        std::int64_t recover_ns = 4'000'000'000;
    };
    static constexpr std::uint32_t kSegmentFrames = kChunksPerSegment;

    static constexpr std::uint8_t kSustainWindows = 3;

    static constexpr std::uint8_t kRecoverDoublings = 3;

    static constexpr std::int64_t kFlushNs = 250'000'000;
    static constexpr std::size_t kMinSlotBytes = 512u * 1024u;

    explicit AviEncoder(const Wiring& w) noexcept : w_(w) {}
    AviEncoder(const Wiring& w, const Limits& l) noexcept : w_(w), lim_(l) {}
    AviEncoder(const AviEncoder&) = delete;
    AviEncoder& operator=(const AviEncoder&) = delete;

    struct Position {
        std::int32_t segment = -1;
        std::int32_t frame = -1;
        bool lost = false;
    };

    [[nodiscard]] bool open(const RawFrameSlot& s) noexcept;
    [[nodiscard]] bool close(const RawFrameSlot& s) noexcept;

    [[nodiscard]] bool chunk(const RawFrameSlot& s, bool real, std::uint64_t core_frame,
                             std::int32_t movie_frame) noexcept;
    [[nodiscard]] Position position() const noexcept { return pos_; }

    void note_held() noexcept { held_ = true; }

    void tend() noexcept;
    [[nodiscard]] int park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const EncodeStatus::Video& counts() const noexcept { return st_; }
    [[nodiscard]] bool take_dirty() noexcept;

    [[nodiscard]] static constexpr bool same_rate(std::uint32_t a, std::uint32_t b) noexcept {
        const std::uint64_t d = a > b ? a - b : b - a;
        return d * 1000u <= b;
    }

    [[nodiscard]] static RecPath segment_path(const RecPath& sidecar, std::uint16_t index) noexcept;

private:
    [[nodiscard]] bool open_segment_(std::uint32_t vtime) noexcept;
    [[nodiscard]] bool marker_(ChunkKind kind, std::uint32_t vtime) noexcept;
    [[nodiscard]] std::byte* reserve_(std::size_t payload_max) noexcept;
    void commit_(std::size_t payload, bool key) noexcept;
    void flush_() noexcept;
    [[nodiscard]] bool all_home_() const noexcept;
    [[nodiscard]] bool size_roll_(std::size_t payload_max) const noexcept;
    void budget_(std::int64_t cpu_ns, std::int64_t now) noexcept;
    void step_half_(bool behind, std::int64_t now) noexcept;

    [[nodiscard]] bool too_soon_(std::int64_t now) const noexcept;

    void recover_(std::size_t queued, std::size_t depth, std::int64_t now) noexcept;
    [[nodiscard]] std::int64_t frame_budget_ns_(std::uint32_t vtime) const noexcept;
    [[nodiscard]] std::int64_t now_() const noexcept;
    [[nodiscard]] std::int64_t cpu_now_() const noexcept;
    [[nodiscard]] bool kept_(std::uint64_t core, std::int32_t movie) const noexcept;
    [[nodiscard]] IFrameCodec& codec_for_(RecCodec c) noexcept;
    [[nodiscard]] bool chunk_one_(IFrameCodec& codec, const RawFrameSlot& s, bool real,
                                  std::uint32_t cand, std::uint32_t run, bool stable) noexcept;

    Wiring w_;
    Limits lim_{};
    CscdCodec cscd_{};
    ZmbvCodec zmbv_{};
    RecOptions opt_{};
    std::uint64_t seg_limit_ = 0;
    IFrameCodec* codec_ = &cscd_;
    bool step_ = true;
    std::uint16_t frame_mul_ = 1;
    FrameArena half_{};
    FrameArena chunks_{};
    std::size_t data_cap_ = 0;
    EncodeStatus::Video st_{};
    bool dirty_ = false;

    bool active_ = false;
    std::uint16_t gen_ = 0;
    RecPath sidecar_{};
    std::uint8_t scale_ = 1;
    bool need_roll_ = false;
    bool broken_ = false;

    bool seg_open_ = false;
    std::uint16_t seg_next_ = 0;
    std::uint64_t seg_bytes_ = 0;
    std::uint32_t seg_frames_ = 0;

    std::uint32_t seg_rate_ = 0;
    std::uint32_t roll_rate_ = 0;
    std::uint32_t cand_ = 0;
    std::uint32_t cand_run_ = 0;
    bool need_key_ = true;
    std::uint32_t since_key_ = 0;
    ChunkChannel::Loan loan_{};
    std::byte* data_ = nullptr;
    std::uint32_t* desc_ = nullptr;
    std::int64_t loan_ns_ = 0;
    Position pos_{};
    bool held_ = false;

    enum class HalfCause : std::uint8_t { None, Queue, Held };
    HalfCause cause_ = HalfCause::None;
    std::int64_t calm_ns_ = 0;
    std::int64_t returned_ns_ = -1;

    std::int64_t win_start_ = -1;
    std::int64_t win_cpu_ = 0;
    std::uint8_t win_over_ = 0;
};

}  // namespace mister::app
