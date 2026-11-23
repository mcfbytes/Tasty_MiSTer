// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/minimig/a2065_window.h"
#include "cores/minimig/lance_bits.h"
#include "cores/minimig/lance_csr.h"
#include "cores/minimig/lance_events.h"
#include "cores/minimig/lance_filter.h"
#include "cores/minimig/lance_ring.h"
#include "cores/minimig/lance_word.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "svc/nic_mac.h"

namespace mister::cores::minimig {

class LanceRegisters {
    TASTY_SEAT_RESIDENT(Nic);

public:
    LanceRegisters(A2065Window& ram, ILanceEvents& events) noexcept : ram_(ram), events_(events) {
        reset();
    }

    LanceRegisters(const LanceRegisters&) = delete;
    LanceRegisters& operator=(const LanceRegisters&) = delete;

    void reset() noexcept;

    void select(LanceWord rap) noexcept { rap_ = LanceCsr::selected(rap); }

    void write(LanceWord v) noexcept;

    void rethink() noexcept;

    [[nodiscard]] LanceWord csr(LanceCsr n) const noexcept { return csr_[n]; }

    [[nodiscard]] LanceWord csr0() const noexcept { return csr(LanceCsr::kStatus); }

    void raise(LanceWord csr0_bits) noexcept;
    void lower(LanceWord csr0_bits) noexcept;
    [[nodiscard]] A2065Window::Shadow shadow() const noexcept;

    [[nodiscard]] bool interrupt_line() const noexcept {
        return csr0().contains(LanceBits::Csr0::kRaised);
    }

    [[nodiscard]] LanceWord mode() const noexcept { return mode_; }

    [[nodiscard]] bool promiscuous() const noexcept {
        return mode_.intersects(LanceBits::Mode::kProm);
    }

    [[nodiscard]] bool loopback() const noexcept {
        return mode_.intersects(LanceBits::Mode::kLoop);
    }

    [[nodiscard]] LanceFilter filter() const noexcept { return filter_; }

    [[nodiscard]] bool initialised() const noexcept { return initialised_; }

    [[nodiscard]] LanceRing& rx() noexcept { return rx_; }

    [[nodiscard]] LanceRing& tx() noexcept { return tx_; }

    [[nodiscard]] const svc::MacAddress& station() const noexcept { return station_; }

    void set_station(svc::MacAddress station) noexcept { station_ = station; }

private:
    void write_status_(LanceWord v) noexcept;

    void init_() noexcept;

    void mask_rings_() noexcept;

    void start_units_() noexcept;

    A2065Window& ram_;
    ILanceEvents& events_;
    infra::IndexedArray<LanceWord, LanceCsr::kCount, LanceCsr> csr_{};
    LanceCsr rap_{};
    bool initialised_ = false;
    LanceWord mode_{};
    LanceFilter filter_{};
    LanceRing rx_{};
    LanceRing tx_{};
    svc::MacAddress station_{};
};

}
