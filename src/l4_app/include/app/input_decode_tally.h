// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "infra/seat.h"

namespace mister::app {

class InputDecodeTally {
    TASTY_SEAT_RESIDENT(Input);

public:
    static void bump(std::atomic<std::uint32_t>& c, std::uint32_t by) noexcept {
        c.store(c.load(std::memory_order_relaxed) + by, std::memory_order_relaxed);
    }

    void round() noexcept { bump(rounds_, 1); }

    void events(std::uint32_t n) noexcept { bump(events_, n); }

    void key_published() noexcept { bump(keys_pub_, 1); }

    void key_emitted() noexcept { bump(keys_emit_, 1); }

    void mouse_packet() noexcept { bump(mouse_pkt_, 1); }

    void kick() noexcept { bump(kicks_, 1); }

    void eviction() noexcept { bump(evicted_, 1); }

    void capture() noexcept { bump(captures_, 1); }

    [[nodiscard]] std::uint32_t joy_seen() const noexcept {
        return joy_seen_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t joy_players() const noexcept {
        return joy_players_.load(std::memory_order_relaxed);
    }

    void store_joy(std::uint32_t seen, std::uint32_t players) noexcept {
        joy_seen_.store(seen, std::memory_order_relaxed);
        joy_players_.store(players, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t rounds() const noexcept {
        return rounds_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t events() const noexcept {
        return events_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t keys_published() const noexcept {
        return keys_pub_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t keys_emitted() const noexcept {
        return keys_emit_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t mouse_packets() const noexcept {
        return mouse_pkt_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t kicks() const noexcept {
        return kicks_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t evictions() const noexcept {
        return evicted_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t captures() const noexcept {
        return captures_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint32_t> rounds_{0};
    std::atomic<std::uint32_t> events_{0};
    std::atomic<std::uint32_t> keys_pub_{0};
    std::atomic<std::uint32_t> keys_emit_{0};
    std::atomic<std::uint32_t> mouse_pkt_{0};
    std::atomic<std::uint32_t> kicks_{0};
    std::atomic<std::uint32_t> evicted_{0};
    std::atomic<std::uint32_t> joy_seen_{0};
    std::atomic<std::uint32_t> joy_players_{0};
    std::atomic<std::uint32_t> captures_{0};
};

}
