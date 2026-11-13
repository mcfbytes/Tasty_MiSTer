// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "app/avi_format.h"
#include "app/avi_write_status.h"
#include "app/chunk_slot.h"
#include "app/frame_arena.h"
#include "app/frame_ticks.h"
#include "app/rec_every.h"
#include "infra/errno.h"
#include "infra/error.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::app {

class AviWriter {
    TASTY_SEAT_RESIDENT(RecWrite);

public:
    struct Wiring {

        ChunkChannel& in;
        AviWriteStatusCell& status;
    };

    static constexpr unsigned kJobsPerPass = 4;

    static constexpr AviFrameCount kMaxEntries = AviFrameCount{kChunksPerSegment};

    static_assert(
        std::uint64_t{kChunksPerSegment} + 1
        <= std::numeric_limits<std::size_t>::max() / avi::kIndexEntry
    );

    explicit AviWriter(const Wiring& w) noexcept : w_(w) {}

    AviWriter(const AviWriter&) = delete;
    AviWriter& operator=(const AviWriter&) = delete;

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept;

    void release() noexcept;

    [[nodiscard]] const AviWriteStatus& status() const noexcept { return st_; }

    void on(const ChunkSlot::Open& s, ControlGeneration gen) noexcept;
    void on(const ChunkSlot::Data& s, ControlGeneration gen) noexcept;
    void on(const ChunkSlot::Close& s, ControlGeneration gen) noexcept;

    void on(const ChunkSlot::Refuse& s, ControlGeneration gen) noexcept;
    void misrouted(const ChunkSlot& m) noexcept;

private:
    void adopt_(ControlGeneration gen) noexcept;
    void finalize_() noexcept;

    void learn_rate_(FrameTicks rate) noexcept;
    void write_header_() noexcept;
    void sync_() noexcept;
    void fail_(infra::Errno err) noexcept;
    [[nodiscard]] bool write_all_(std::span<const std::byte> bytes) noexcept;

    Wiring w_;
    UniqueFd fd_{};
    FrameArena index_{};
    AviFrameCount entries_{};
    avi::Fields f_{};
    std::uint64_t pos_ = 0;
    bool rate_known_ = false;
    RecEvery frame_mul_{};

    struct Unrated {
        UniqueFd fd{};
        avi::Fields f{};
    };

    static constexpr std::size_t kUnrated = 4;
    std::array<Unrated, kUnrated> unrated_{};
    std::size_t nunrated_ = 0;
    void drop_unrated_() noexcept;
    AviWriteStatus st_{};
    bool dirty_ = false;
};

}
