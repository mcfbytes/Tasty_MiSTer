// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/minimig/lance_word.h"
#include "svc/nic_mac.h"

namespace mister::cores::minimig {

struct LanceCsr {
    static constexpr std::size_t kCount = 128;
    std::uint8_t v = 0;

    enum class Rule : std::uint8_t {
        Status,
        InitLow,
        InitHigh,
        Bus,
        Filter,
        Station,
        Mode,
        RxBaseLow,
        RxBaseHigh,
        TxBaseLow,
        TxBaseHigh,
        RxLength,
        TxLength,
        Plain,
    };

    static const LanceCsr kStatus;
    static const LanceCsr kInitLow;
    static const LanceCsr kInitHigh;
    static const LanceCsr kBus;
    static const LanceCsr kTest;
    static const LanceCsr kFilterFirst;
    static const LanceCsr kFilterLast;
    static const LanceCsr kStationFirst;
    static const LanceCsr kStationLast;
    static const LanceCsr kMode;
    static const LanceCsr kRxBaseLow;
    static const LanceCsr kRxBaseHigh;
    static const LanceCsr kTxBaseLow;
    static const LanceCsr kTxBaseHigh;
    static const LanceCsr kRxLength;
    static const LanceCsr kTxLength;

    [[nodiscard]] static constexpr LanceCsr selected(LanceWord rap) noexcept {
        constexpr std::uint16_t kSelectMask = kCount - 1;
        return LanceCsr{static_cast<std::uint8_t>(rap.v & kSelectMask)};
    }

    [[nodiscard]] constexpr Rule rule() const noexcept;

    [[nodiscard]] std::span<std::byte, sizeof(LanceWord)> station_bytes(
        svc::MacAddress& station
    ) const noexcept {
        const std::size_t at = static_cast<std::size_t>(v - kStationFirst.v) * sizeof(LanceWord);
        return std::span<std::byte>(station.bytes).subspan(at).first<sizeof(LanceWord)>();
    }

    friend constexpr bool operator==(LanceCsr, LanceCsr) = default;
    friend constexpr auto operator<=>(LanceCsr, LanceCsr) = default;
};

inline constexpr LanceCsr LanceCsr::kStatus{0};
inline constexpr LanceCsr LanceCsr::kInitLow{1};
inline constexpr LanceCsr LanceCsr::kInitHigh{2};
inline constexpr LanceCsr LanceCsr::kBus{3};
inline constexpr LanceCsr LanceCsr::kTest{4};
inline constexpr LanceCsr LanceCsr::kFilterFirst{8};
inline constexpr LanceCsr LanceCsr::kFilterLast{11};
inline constexpr LanceCsr LanceCsr::kStationFirst{12};
inline constexpr LanceCsr LanceCsr::kStationLast{14};
inline constexpr LanceCsr LanceCsr::kMode{15};
inline constexpr LanceCsr LanceCsr::kRxBaseLow{24};
inline constexpr LanceCsr LanceCsr::kRxBaseHigh{25};
inline constexpr LanceCsr LanceCsr::kTxBaseLow{30};
inline constexpr LanceCsr LanceCsr::kTxBaseHigh{31};
inline constexpr LanceCsr LanceCsr::kRxLength{76};
inline constexpr LanceCsr LanceCsr::kTxLength{78};

constexpr LanceCsr::Rule LanceCsr::rule() const noexcept {
    if (*this == kStatus)
        return Rule::Status;
    if (*this == kInitLow)
        return Rule::InitLow;
    if (*this == kInitHigh)
        return Rule::InitHigh;
    if (*this == kBus)
        return Rule::Bus;
    if (*this >= kFilterFirst && *this <= kFilterLast)
        return Rule::Filter;
    if (*this >= kStationFirst && *this <= kStationLast)
        return Rule::Station;
    if (*this == kMode)
        return Rule::Mode;
    if (*this == kRxBaseLow)
        return Rule::RxBaseLow;
    if (*this == kRxBaseHigh)
        return Rule::RxBaseHigh;
    if (*this == kTxBaseLow)
        return Rule::TxBaseLow;
    if (*this == kTxBaseHigh)
        return Rule::TxBaseHigh;
    if (*this == kRxLength)
        return Rule::RxLength;
    if (*this == kTxLength)
        return Rule::TxLength;
    return Rule::Plain;
}

}
