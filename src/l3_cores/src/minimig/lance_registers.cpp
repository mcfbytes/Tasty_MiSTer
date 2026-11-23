// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/lance_registers.h"

#include <algorithm>
#include <cstdint>
#include <span>

#include "cores/minimig/lance_addr.h"
#include "cores/minimig/lance_span.h"

namespace mister::cores::minimig {

namespace {

using Csr0 = LanceBits::Csr0;
using Mode = LanceBits::Mode;

constexpr LanceWord kTestDefault{0x0115};
constexpr LanceWord kInitLowKeep{0xFFFE};
constexpr LanceWord kInitHighKeep{0x00FF};
constexpr LanceWord kBusKeep{0x0007};

constexpr LanceSpan kInitMode{0};
constexpr LanceSpan kInitStation{2};
constexpr LanceSpan kInitFilter{8};
constexpr LanceSpan kInitRxRing{16};
constexpr LanceSpan kInitTxRing{20};
constexpr LanceSpan kWord{2};

}

void LanceRegisters::reset() noexcept {
    std::ranges::fill(csr_, LanceWord{});
    csr_[LanceCsr::kStatus] = Csr0::kStop;
    csr_[LanceCsr::kTest] = kTestDefault;
    rap_ = LanceCsr{};
    initialised_ = false;
    tx_.cursor = LanceRingSlot{};
    rx_.cursor = LanceRingSlot{};
}

void LanceRegisters::raise(LanceWord bits) noexcept {
    LanceWord& c0 = csr_[LanceCsr::kStatus];
    c0 = c0.with(bits);
}

void LanceRegisters::lower(LanceWord bits) noexcept {
    LanceWord& c0 = csr_[LanceCsr::kStatus];
    c0 = c0.without(bits);
}

A2065Window::Shadow LanceRegisters::shadow() const noexcept {
    return {
        csr(LanceCsr::kStatus),
        csr(LanceCsr::kInitLow),
        csr(LanceCsr::kInitHigh),
        csr(LanceCsr::kBus)
    };
}

void LanceRegisters::rethink() noexcept {
    lower(Csr0::kIntr);
    if (csr0().intersects(Csr0::kCauses))
        raise(Csr0::kIntr);
    if (interrupt_line())
        events_.interrupt();
}

void LanceRegisters::write(LanceWord v) noexcept {
    using Rule = LanceCsr::Rule;
    LanceWord& reg = csr_[rap_];
    const bool stopped = csr0().intersects(Csr0::kStop);
    switch (rap_.rule()) {
        case Rule::Status:
            write_status_(v);
            return;
        case Rule::InitLow:
            if (stopped)
                reg = v.masked(kInitLowKeep);
            return;
        case Rule::InitHigh:
            if (stopped)
                reg = v.masked(kInitHighKeep);
            return;
        case Rule::Bus:
            if (stopped)
                reg = v.masked(kBusKeep);
            return;
        case Rule::Filter:
            filter_ = filter_.with_register(rap_, v);
            return;
        case Rule::Station:
            v.store_le(rap_.station_bytes(station_));
            return;
        case Rule::Mode:
            mode_ = v;
            return;
        case Rule::RxBaseLow:
            rx_.base = rx_.base.with_low(v);
            mask_rings_();
            return;
        case Rule::RxBaseHigh:
            rx_.base = rx_.base.with_high(v);
            mask_rings_();
            return;
        case Rule::TxBaseLow:
            tx_.base = tx_.base.with_low(v);
            mask_rings_();
            return;
        case Rule::TxBaseHigh:
            tx_.base = tx_.base.with_high(v);
            mask_rings_();
            return;
        case Rule::RxLength:
            rx_.length = LanceRingLength::of_negated(v);
            return;
        case Rule::TxLength:
            tx_.length = LanceRingLength::of_negated(v);
            return;
        case Rule::Plain:
            reg = v;
            return;
    }
}

void LanceRegisters::write_status_(LanceWord v) noexcept {
    const LanceWord before = csr0();
    LanceWord& c0 = csr_[LanceCsr::kStatus];
    c0 = c0.without(Csr0::kInea).with(v.masked(Csr0::kInea));
    c0 = c0.with(v.masked(Csr0::kCommands));
    c0 = c0.without(v.masked(Csr0::kAcks));
    c0 = c0.without(Csr0::kErr);

    const auto rose = [&](LanceWord bit) {
        return c0.intersects(bit) && !before.intersects(bit);
    };
    if (rose(Csr0::kStop)) {
        c0 = Csr0::kStop;
        csr_[LanceCsr::kBus] = LanceWord{};
    } else if (rose(Csr0::kStrt) && before.intersects(Csr0::kStop.with(Csr0::kInit))) {

        c0 = c0.without(Csr0::kStop);
        start_units_();
        if (rose(Csr0::kInit)) {
            init_();
            raise(Csr0::kIdon);
            initialised_ = true;
        }
    } else if (rose(Csr0::kInit) && before.intersects(Csr0::kStop)) {
        init_();
        raise(Csr0::kIdon);
        lower(Csr0::kRxon.with(Csr0::kTxon).with(Csr0::kStop));
        initialised_ = true;
        csr_[LanceCsr::kBus] = LanceWord{};
    }

    if (csr0().intersects(Csr0::kTdmd)) {
        if (initialised_) {
            if (!csr0().intersects(Csr0::kTxon)) {
                lower(Csr0::kStop);
                start_units_();
            }
            events_.transmit_demand();
        }
        lower(Csr0::kTdmd);
    }
    rethink();
}

void LanceRegisters::start_units_() noexcept {
    if (!mode_.intersects(Mode::kDtx))
        raise(Csr0::kTxon);
    if (!mode_.intersects(Mode::kDrx))
        raise(Csr0::kRxon);
}

void LanceRegisters::init_() noexcept {
    const LanceAddr block = LanceAddr::of_words(
                                {.low = csr(LanceCsr::kInitLow),
                                 .high = csr(LanceCsr::kInitHigh).masked(kInitHighKeep)}
    )
                                .in_ram();
    mode_ = ram_.ram_word(block + kInitMode);
    LanceFilter::Quarters quarters{};
    for (std::uint32_t q = 0; q < LanceFilter::kQuarters; ++q)
        quarters[q] = ram_.ram_word(block + kInitFilter + kWord * q);
    filter_ = LanceFilter::of(quarters);
    const LanceAddr rdr = LanceAddr::of_words(ram_.ram_pair(block + kInitRxRing));
    const LanceAddr tdr = LanceAddr::of_words(ram_.ram_pair(block + kInitTxRing));
    rx_.length = LanceRingLength::of_ring_field(rdr);
    tx_.length = LanceRingLength::of_ring_field(tdr);
    rx_.base = rdr.ring_base();
    tx_.base = tdr.ring_base();

    constexpr std::uint32_t kStationPairs = svc::MacAddress::kBytes / sizeof(LanceWord);
    const std::span<std::byte> station(station_.bytes);
    for (std::uint32_t pair = 0; pair < kStationPairs; ++pair) {
        ram_.ram_word(block + kInitStation + kWord * pair)
            .store_le(station.subspan(pair * sizeof(LanceWord)).first<sizeof(LanceWord)>());
    }
    events_.initialised(station_);
    mask_rings_();
}

void LanceRegisters::mask_rings_() noexcept {
    rx_.base = rx_.base.in_ram();
    tx_.base = tx_.base.in_ram();
    rx_.cursor = LanceRingSlot{};
    tx_.cursor = LanceRingSlot{};
}

}
