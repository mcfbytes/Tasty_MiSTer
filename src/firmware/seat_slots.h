// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tuple>

#include "infra/seat.h"

namespace mister::fw {

template <class... Rows>
struct SeatList {};

template <class List>
class SeatSlots;

template <class... Rows>
class SeatSlots<SeatList<Rows...>> {
public:
    constexpr SeatSlots() noexcept = default;

    template <class... M>
        requires(sizeof...(M) > 0)
    constexpr explicit SeatSlots(M*... mains) noexcept {
        (bind(mains), ...);
    }

    template <class M>
    constexpr void bind(M* main) noexcept {
        std::get<M*>(slots_) = main;
    }

    template <class M>
    [[nodiscard]] constexpr M* get() const noexcept {
        return std::get<M*>(slots_);
    }

    [[nodiscard]] constexpr bool bound(SeatTag s) const noexcept {
        bool hit = false;
        ((Rows::seat == s ? (hit = get<typename Rows::Main>() != nullptr) : false), ...);
        return hit;
    }

private:
    std::tuple<typename Rows::Main*...> slots_{};
};

}
