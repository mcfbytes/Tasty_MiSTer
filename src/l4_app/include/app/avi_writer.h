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

namespace mister::app {

class AviWriter {
    TASTY_SEAT_RESIDENT(RecWrite);

public:
    struct Wiring {
        ChunkChannel* in = nullptr;
        AviWriteStatusCell* status = nullptr;
    };

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

    void refuse_(const ChunkSlot& s) noexcept;
    void finalize_() noexcept;

    void learn_rate_(std::uint32_t vtime) noexcept;
    [[nodiscard]] std::uint32_t scaled_vtime_(std::uint32_t vtime) const noexcept;
    void write_header_() noexcept;
    void sync_() noexcept;
    void fail_(int err) noexcept;
    [[nodiscard]] bool write_all_(const std::byte* p, std::size_t n) noexcept;

    Wiring w_;
    UniqueFd fd_{};
    FrameArena index_{};
    std::uint32_t entries_ = 0;
    avi::Fields f_{};
    std::uint64_t pos_ = 0;
    bool rate_known_ = false;
    std::uint16_t frame_mul_ = 1;

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

}  // namespace mister::app
