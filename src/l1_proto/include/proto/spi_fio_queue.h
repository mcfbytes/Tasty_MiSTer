// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/boards_table.h"
#include "hal/link_timing.h"
#include "hal/spi_transport.h"
#include "proto/download_session.h"
#include "proto/fio_window.h"
#include "proto/reset_fence.h"
#include "proto/reset_terms.h"
#include "proto/types.h"
#include "reactor/tick.h"

namespace mister::proto {

class SpiFioQueue final : public IResetFence {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kSlabBytes = 5120;
    static constexpr std::size_t kWindows = 32;
    static constexpr std::size_t kMaxBrackets = 2;
    static constexpr std::size_t kMaxCommandWords = 8;

    static constexpr std::uint32_t kPieceWords = 392;
    static constexpr std::uint32_t kUnbudgeted = std::numeric_limits<std::uint32_t>::max();

    struct Bracket {
        WideIoIndex index{};
        std::span<const std::uint8_t> bytes{};
    };

    struct Stats {
        std::uint32_t acts = 0;
        std::uint32_t windows = 0;
        std::uint32_t words = 0;
        std::uint32_t abandons = 0;
        std::uint32_t wire_faults = 0;
        std::uint32_t busy = 0;
        std::uint32_t oversize = 0;
        std::uint32_t flushes = 0;
        std::uint32_t resets = 0;
    };

    explicit SpiFioQueue(hal::ISpiTransport& link,
                         std::uint32_t piece_words = kPieceWords) noexcept;
    SpiFioQueue(const SpiFioQueue&) = delete;
    SpiFioQueue& operator=(const SpiFioQueue&) = delete;

    [[nodiscard]] static constexpr std::size_t windows_for(std::size_t bytes,
                                                           std::size_t piece_bytes) noexcept {
        return 3u + (bytes == 0 ? 1u : (bytes + piece_bytes - 1u) / piece_bytes);
    }

    [[nodiscard]] bool fits(std::size_t windows, std::size_t bytes,
                            std::size_t brackets) const noexcept;

    [[nodiscard]] std::size_t bracket_windows(std::size_t bytes) const noexcept;

    [[nodiscard]] Ex<void> submit(std::span<const Bracket> act);

    [[nodiscard]] Ex<void> append(std::span<const Bracket> act);

    [[nodiscard]] Ex<void> append_command(std::span<const std::uint16_t> words);

    std::uint32_t drain(std::uint32_t budget_words) noexcept;

    std::uint32_t flush() noexcept;

    void abandon() noexcept;

    void hold() noexcept { held_ = true; }
    void release() noexcept { held_ = false; }
    [[nodiscard]] bool held() const noexcept { return held_; }

    void declare_resets(const ResetTerms& terms) noexcept;

    void before_status(const StatusWord& sent, const StatusWord& next) noexcept override;
    void before_buttons(std::uint16_t sent, std::uint16_t next) noexcept override;
    void before_reset_line() noexcept override;

    [[nodiscard]] bool idle() const noexcept {
        TASTY_SEAT_BODY(SpiFioQueue);
        return head_ == size_;
    }
    [[nodiscard]] Stats stats() const noexcept {
        TASTY_SEAT_BODY(SpiFioQueue);
        return stats_;
    }
    [[nodiscard]] std::uint32_t piece_words() const noexcept { return piece_words_; }

private:
    struct Emitter {
        TASTY_SEAT_RESIDENT(RT);
        using Result = Ex<void>;
        hal::ISpiTransport* link;
        const std::uint8_t* slab;
        std::uint16_t latched = 0;
        std::optional<DownloadSession> open{};
        [[nodiscard]] Ex<void> on(const FioWindow::Index& i) noexcept;
        [[nodiscard]] Ex<void> on(const FioWindow::Open&) noexcept;
        [[nodiscard]] Ex<void> on(const FioWindow::Data& d) noexcept;
        [[nodiscard]] Ex<void> on(const FioWindow::Close&) noexcept;
        [[nodiscard]] Ex<void> on(const FioWindow::IoCommand& c) noexcept;
        [[nodiscard]] Ex<void> misrouted(const FioWindow&) noexcept;
    };
    struct Cost;
    [[nodiscard]] std::uint32_t cost_(const FioWindow& w) const noexcept;
    void abandon_() noexcept;
    void reset_abandon_() noexcept;
    [[nodiscard]] Ex<void> place_(std::span<const Bracket> act);
    void restart_if_idle_() noexcept;

    hal::ISpiTransport* link_;
    std::uint32_t piece_words_;
    std::array<std::uint8_t, kSlabBytes> slab_{};
    std::array<FioWindow, kWindows> q_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::size_t slab_used_ = 0;
    Emitter wire_{link_, slab_.data()};
    Stats stats_{};
    ResetTerms terms_ = ResetTerms::framework();
    bool held_ = false;
};

consteval std::uint32_t tightest_round_budget_words() {
    std::uint32_t words = SpiFioQueue::kUnbudgeted;
    for (const hal::BoardProfile* p : hal::board_table()) {
        if (!hal::all_measured(p->timing)) continue;
        const std::uint32_t b = hal::LinkTimingValues::measured(p->timing).round_budget_words;
        words = b < words ? b : words;
    }
    return words;
}

consteval std::uint32_t tightest_load_budget_words() {
    std::uint32_t words = SpiFioQueue::kUnbudgeted;
    for (const hal::BoardProfile* p : hal::board_table()) {
        if (!hal::all_measured(p->timing)) continue;
        const std::uint32_t b = hal::LinkTimingValues::measured(p->timing).load_budget_words;
        words = b < words ? b : words;
    }
    return words;
}

consteval bool load_budget_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const auto t = hal::LinkTimingValues::measured(g);
        return t.load_budget_words >= t.round_budget_words &&
               std::uint64_t{t.load_budget_words} * t.word_ns <= hal::kLoadWireNs;
    });
}
static_assert(load_budget_fits_every_board(),
              "a load round holds a gameplay round, and its wire stays within about 10 ms");

consteval bool fio_budget_fits_every_board() {
    return hal::every_measured(&hal::BoardProfile::timing, [](const hal::LinkTiming& g) consteval {
        const auto t = hal::LinkTimingValues::measured(g);
        return 1 + SpiFioQueue::kPieceWords <= t.round_budget_words &&
               hal::round_cost_ns(t.word_ns, t.base_round_ns, t.sleep_ns, t.round_budget_words) <
                   static_cast<std::uint64_t>(reactor::kTickNs);
    });
}
static_assert(fio_budget_fits_every_board(),
              "every Data window fits one round, and a budgeted round stays under 1 ms");

}  // namespace mister::proto
