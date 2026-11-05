// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "app/link_rx_channel.h"
#include "app/save_extent_cell.h"
#include "cores/save_extent.h"
#include "infra/seat.h"

namespace mister::app {

class OwedSaveBind {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Cell = SaveExtentCell;

    OwedSaveBind(Cell& cell, LinkRxChannel<SeatTag::Main>& rx) noexcept : cell_(cell), rx_(rx) {}

    OwedSaveBind(const OwedSaveBind&) = delete;
    OwedSaveBind& operator=(const OwedSaveBind&) = delete;
    OwedSaveBind(OwedSaveBind&&) = delete;

    ~OwedSaveBind() { strand(); }

    [[nodiscard]] bool owed() const noexcept { return static_cast<bool>(gen_); }

    [[nodiscard]] proto::ActKey generation() const noexcept { return gen_; }

    void arm(proto::ActKey generation, const cores::SaveExtent& on_strand) noexcept {
        if (gen_ && gen_ != generation)
            strand();
        gen_ = generation;
        strand_body_ = on_strand;
    }

    void answer(cores::SaveVerdict v, const cores::SaveExtent& payload) noexcept {
        if (!gen_)
            return;
        publish_(std::exchange(gen_, proto::ActKey{}), v, payload);
    }

    void strand() noexcept {
        if (!gen_)
            return;
        publish_(std::exchange(gen_, proto::ActKey{}), cores::SaveVerdict::Declined, strand_body_);
    }

private:
    void publish_(proto::ActKey generation, cores::SaveVerdict v, cores::SaveExtent body) noexcept {
        set_verdict(body, v);
        body.generation = generation;
        cell_.publish(body);
        rx_.request_wake();
    }

    Cell& cell_;
    LinkRxChannel<SeatTag::Main>& rx_;
    cores::SaveExtent strand_body_{};
    proto::ActKey gen_{};
};

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
