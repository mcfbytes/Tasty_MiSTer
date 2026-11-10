// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "app/link_rx_channel.h"
#include "app/save_extent_cell.h"
#include "cores/save_extent.h"
#include "infra/seat.h"

namespace mister::app {

template <class CellT, auto kStrand>
class OwedAnswer {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Cell = CellT;
    using Level = typename Cell::value_type;
    using Verdict = decltype(kStrand);

    OwedAnswer(Cell& cell, LinkRxChannel<SeatTag::Main>& rx) noexcept : cell_(cell), rx_(rx) {}

    OwedAnswer(const OwedAnswer&) = delete;
    OwedAnswer& operator=(const OwedAnswer&) = delete;
    OwedAnswer(OwedAnswer&&) = delete;

    ~OwedAnswer() { strand(); }

    [[nodiscard]] bool owed() const noexcept { return static_cast<bool>(gen_); }

    [[nodiscard]] proto::ActKey generation() const noexcept { return gen_; }

    void arm(proto::ActKey generation, const Level& on_strand) noexcept {
        if (gen_ && gen_ != generation)
            strand();
        gen_ = generation;
        strand_body_ = on_strand;
    }

    void answer(Verdict v, const Level& payload) noexcept {
        if (!gen_)
            return;
        publish_(std::exchange(gen_, proto::ActKey{}), v, payload);
    }

    void strand() noexcept {
        if (!gen_)
            return;
        publish_(std::exchange(gen_, proto::ActKey{}), kStrand, strand_body_);
    }

private:
    void publish_(proto::ActKey generation, Verdict v, Level body) noexcept {
        set_verdict(body, v);
        body.generation = generation;
        cell_.publish(body);
        rx_.request_wake();
    }

    Cell& cell_;
    LinkRxChannel<SeatTag::Main>& rx_;
    Level strand_body_{};
    proto::ActKey gen_{};
};

using OwedSaveBind = OwedAnswer<SaveExtentCell, cores::SaveVerdict::Declined>;

template <std::size_t N>
[[nodiscard]] std::array<OwedSaveBind, N> make_owed_saves(
    SaveExtentCell& cell,
    LinkRxChannel<SeatTag::Main>& rx
) noexcept {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<OwedSaveBind, N>{((void)I, OwedSaveBind{cell, rx})...};
    }(std::make_index_sequence<N>{});
}

}
