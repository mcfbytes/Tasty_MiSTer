// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/cell_gen.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/xthread/seq_cell.h"

namespace mister::infra::xthread {

template <class T, SeatTag Owner = SeatTag::Unbound, unsigned Attempts = 2>
class Telemetry {
    TASTY_SEAT_MEDIATOR(Any, Any);
    static_assert(
        std::is_trivially_copyable_v<T>,
        "the body crosses as its bytes, in words: a cell "
        "payload is a POD, never an owning type"
    );
    static_assert(std::is_nothrow_default_constructible_v<T>);
    static_assert(Attempts >= 1, "a bounded read needs at least one attempt");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

public:
    using value_type = T;

    struct Sample {
        T value{};
        infra::CellGen generation{};

        explicit constexpr operator bool() const noexcept { return generation.held(); }
    };

    class Reader {
        TASTY_SEAT_EXEMPT(component);

    public:
        [[nodiscard]] std::optional<T> take_if_changed(const Telemetry& cell) noexcept {
            const infra::CellGen g = cell.generation();
            if (!g.held() || g == seen_)
                return std::nullopt;
            const Sample s = cell.sample();
            if (!s)
                return std::nullopt;
            seen_ = s.generation;
            return s.value;
        }

        [[nodiscard]] bool take_if_changed(const Telemetry& cell, T& dst) noexcept {
            const infra::CellGen g = cell.generation();
            if (!g.held() || g == seen_)
                return false;
            const std::uint32_t got = cell.sample_into(dst);
            if (got == 0)
                return false;
            seen_ = infra::CellGen{got};
            return true;
        }

        [[nodiscard]] bool unread(const Telemetry& cell) const noexcept {
            const infra::CellGen g = cell.generation();
            return g.held() && g != seen_;
        }

        [[nodiscard]] infra::CellGen seen() const noexcept { return seen_; }

        void forget() noexcept { seen_ = infra::CellGen{}; }

    private:
        infra::CellGen seen_{};
    };

    Telemetry() = default;
    Telemetry(const Telemetry&) = delete;
    Telemetry& operator=(const Telemetry&) = delete;
    Telemetry(Telemetry&&) = delete;
    Telemetry& operator=(Telemetry&&) = delete;

    void publish(const T& v) noexcept {
        seat_assert<Owner>(ERR_SITE(), "Telemetry::publish off its owning seat");
        std::uint32_t g = seq_.load(std::memory_order_relaxed) + 1u;
        if (g == 0)
            g = 1;
        seq_.store(g, std::memory_order_relaxed);
        cell_.write(g, v);
    }

    void invalidate() noexcept {
        seat_assert<Owner>(ERR_SITE(), "Telemetry::invalidate off its owning seat");
        cell_.clear();
    }

    [[nodiscard]] std::uint32_t sample_into(T& dst) const noexcept {
        for (unsigned attempt = 0; attempt < Attempts; ++attempt) {
            if (const std::uint32_t g = cell_.try_read(dst))
                return g;
        }
        refused_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    [[nodiscard]] Sample sample() const noexcept {
        Sample s;
        const std::uint32_t g = sample_into(s.value);
        if (g == 0)
            return Sample{};
        s.generation = infra::CellGen{g};
        return s;
    }

    [[nodiscard]] infra::CellGen generation() const noexcept {
        return infra::CellGen{cell_.generation()};
    }

    [[nodiscard]] std::uint32_t refusals() const noexcept {
        return refused_.load(std::memory_order_relaxed);
    }

private:
    SeqCell<T> cell_;
    std::atomic<std::uint32_t> seq_{0};
    mutable std::atomic<std::uint32_t> refused_{0};
};

}
