// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/avi_format.h"
#include "app/avi_write_status.h"
#include "app/chunk_slot.h"
#include "app/frame_arena.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/clock.h"

namespace mister::app {

class AviWriter {
    TASTY_SEAT_RESIDENT(RecWrite);

public:
    struct Wiring {
        ChunkChannel* in = nullptr;
        AviWriteStatusCell* status = nullptr;
        const os::IClock* clock = nullptr;
    };

    static constexpr std::int64_t kHeaderNs = 1'000'000'000;
    static constexpr std::int64_t kSyncNs = 5'000'000'000;
    static constexpr unsigned kJobsPerPass = 4;

    static constexpr std::size_t kMaxEntries = kChunksPerSegment;

    explicit AviWriter(const Wiring& w) noexcept : w_(w) {}
    AviWriter(const AviWriter&) = delete;
    AviWriter& operator=(const AviWriter&) = delete;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const AviWriteStatus& status() const noexcept { return st_; }

private:
    void open_(const ChunkSlot& s) noexcept;
    void data_(const ChunkSlot& s) noexcept;
    void finalize_() noexcept;

    void learn_rate_(std::uint32_t vtime) noexcept;
    void write_header_(std::int64_t now) noexcept;
    void sync_(std::int64_t now) noexcept;
    void fail_(int err) noexcept;
    [[nodiscard]] bool write_all_(const std::byte* p, std::size_t n) noexcept;
    [[nodiscard]] std::int64_t now_() const noexcept;

    Wiring w_;
    UniqueFd fd_{};
    FrameArena index_{};
    std::uint32_t entries_ = 0;
    AviFormat::Fields f_{};
    std::uint64_t pos_ = 0;
    bool rate_known_ = false;

    struct Unrated {
        UniqueFd fd{};
        AviFormat::Fields f{};
    };
    static constexpr std::size_t kUnrated = 4;
    std::array<Unrated, kUnrated> unrated_{};
    std::size_t nunrated_ = 0;
    void drop_unrated_() noexcept;
    std::int64_t header_ns_ = 0;
    std::int64_t synced_ns_ = 0;
    AviWriteStatus st_{};
    bool dirty_ = false;
};

}  // namespace mister::app
