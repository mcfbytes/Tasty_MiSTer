// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <tuple>
#include <utility>

#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::app {

template <class... Lends>
class LwLendSet {
    TASTY_SEAT_RESIDENT(Main);

public:
    template <class... Cells>
    explicit LwLendSet(Cells&&... cells) noexcept : lends_(std::forward<Cells>(cells)...) {}

    LwLendSet(const LwLendSet&) = delete;
    LwLendSet& operator=(const LwLendSet&) = delete;

    template <class Lend>
    [[nodiscard]] Lend& get() noexcept {
        return std::get<Lend>(lends_);
    }

    template <class Lend>
    [[nodiscard]] const Lend& get() const noexcept {
        return std::get<Lend>(lends_);
    }

    [[nodiscard]] bool retire_all() noexcept {
        TASTY_SEAT_BODY(LwLendSet);
        return retire_each_(kEach);
    }

    void abandon_all() noexcept {
        TASTY_SEAT_BODY(LwLendSet);
        abandon_each_(kEach);
    }

    [[nodiscard]] bool any_mapped() const noexcept {
        TASTY_SEAT_BODY(LwLendSet);
        return mapped_any_(kEach);
    }

private:
    static constexpr auto kEach = std::index_sequence_for<Lends...>{};

    template <std::size_t... I>
    [[nodiscard]] bool retire_each_(std::index_sequence<I...>) noexcept {
        bool all = true;
        ((all = (!std::get<I>(lends_).mapped() || std::get<I>(lends_).retire()) && all), ...);
        return all;
    }

    template <std::size_t... I>
    void abandon_each_(std::index_sequence<I...>) noexcept {
        (std::get<I>(lends_).abandon(), ...);
    }

    template <std::size_t... I>
    [[nodiscard]] bool mapped_any_(std::index_sequence<I...>) const noexcept {
        return (std::get<I>(lends_).mapped() || ...);
    }

    std::tuple<Lends...> lends_;
};

}
