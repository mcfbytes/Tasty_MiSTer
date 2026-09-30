// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

    OwedSaveBind() noexcept = default;
    OwedSaveBind(const OwedSaveBind&) = delete;
    OwedSaveBind& operator=(const OwedSaveBind&) = delete;
    OwedSaveBind(OwedSaveBind&&) = delete;

    OwedSaveBind& operator=(OwedSaveBind&& other) noexcept {
        if (this != &other) {
            strand();
            adopt_(other);
        }
        return *this;
    }
    ~OwedSaveBind() { strand(); }

    void bind(Cell& cell, LinkRxChannel* rx) noexcept {
        strand();
        cell_ = &cell;
        rx_ = rx;
    }

    [[nodiscard]] bool owed() const noexcept { return gen_ != 0; }
    [[nodiscard]] std::uint32_t generation() const noexcept { return gen_; }

    void arm(std::uint32_t generation, const cores::SaveExtent& on_strand) noexcept {
        if (gen_ != 0 && gen_ != generation) strand();
        gen_ = generation;
        strand_body_ = on_strand;
    }

    void answer(cores::SaveVerdict v, const cores::SaveExtent& payload) noexcept {
        if (gen_ == 0) return;
        publish_(std::exchange(gen_, 0u), v, payload);
    }

    void strand() noexcept {
        if (gen_ == 0) return;
        publish_(std::exchange(gen_, 0u), cores::SaveVerdict::Declined, strand_body_);
    }

private:
    void publish_(std::uint32_t generation, cores::SaveVerdict v, cores::SaveExtent body) noexcept {
        set_verdict(body, v);
        body.generation = generation;
        if (cell_ != nullptr) cell_->publish(body);
        if (rx_ != nullptr) rx_->request_wake();
    }
    void adopt_(OwedSaveBind& other) noexcept {
        cell_ = other.cell_;
        rx_ = other.rx_;
        strand_body_ = other.strand_body_;
        gen_ = std::exchange(other.gen_, 0u);
    }

    Cell* cell_ = nullptr;
    LinkRxChannel* rx_ = nullptr;
    cores::SaveExtent strand_body_{};
    std::uint32_t gen_ = 0;
};

}  // namespace mister::app
