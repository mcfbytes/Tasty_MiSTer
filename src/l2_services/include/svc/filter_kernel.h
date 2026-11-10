// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "svc/filter_bank_index.h"
#include "svc/filter_format.h"
#include "svc/filter_phase.h"
#include "svc/filter_send_plan.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::size_t kFilterPhases = 256;
inline constexpr std::size_t kFilterTaps = 4;
inline constexpr std::size_t kGeneratedKernelHalves = 2;

inline constexpr std::size_t kFilterCoeffWords = 4096;

inline constexpr std::size_t kFilterBytePhaseStep = 16;
inline constexpr std::size_t kFilterCoarsePhaseStep = 4;
inline constexpr std::size_t kFilterCoarsePhases = 64;
inline constexpr std::size_t kFilterWordsPerEntry = 2;
inline constexpr std::uint32_t kFilterValueMask9 = 0x1FF;
inline constexpr std::uint32_t kFilterValueMask10 = 0x3FF;

inline constexpr std::int16_t kFilterHotFull = 256;

struct FilterTap {
    std::int16_t v = 0;

    [[nodiscard]] constexpr std::uint16_t byte_value() const noexcept {
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>(v >> 1) & kFilterValueMask9);
    }

    [[nodiscard]] constexpr std::uint16_t wide_value(int shift) const noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(v >> shift) & kFilterValueMask10
        );
    }

    friend constexpr bool operator==(FilterTap, FilterTap) = default;
};

struct FilterPhaseIndex {
    std::size_t v = 0;

    template <std::size_t kPhasesPerIteration>
    [[nodiscard]] static constexpr FilterPhaseIndex at(FilterIteration iter) noexcept {
        return FilterPhaseIndex{iter.v * kPhasesPerIteration};
    }

    [[nodiscard]] static constexpr FilterTap tap_value(
        FilterTapIndex tap,
        const std::array<std::int16_t, kFilterTaps>& taps
    ) noexcept {
        return FilterTap{taps[tap.v]};
    }

    [[nodiscard]] constexpr bool lower_half() const noexcept { return v < kFilterPhases / 2; }

    [[nodiscard]] constexpr const std::array<std::int16_t, kFilterTaps>& taps(
        std::span<const FilterPhase> phases
    ) const noexcept {
        return phases[v].t;
    }

    friend constexpr bool operator==(FilterPhaseIndex, FilterPhaseIndex) = default;
};

namespace detail {

[[nodiscard]] constexpr std::size_t filter_words_per_bank(FilterFormat fmt) noexcept {
    switch (fmt.encoding) {
        case FilterEncoding::None:
            return 0;
        case FilterEncoding::Bytes:
            return (kFilterPhases / kFilterBytePhaseStep) * kFilterTaps;
        case FilterEncoding::Words:
        case FilterEncoding::WordsAdaptive: {
            const std::size_t skip = fmt.full_precision ? 1u : kFilterCoarsePhaseStep;
            return (kFilterPhases / skip) * kFilterTaps * kFilterWordsPerEntry;
        }
    }
    return 0;
}

template <class PhaseSource>
[[nodiscard]] constexpr std::uint16_t filter_word(
    FilterFormat fmt,
    const PhaseSource& src,
    FilterBankIndex bank,
    FilterBankWordIndex word
) noexcept {
    switch (fmt.encoding) {
        case FilterEncoding::None:
            return 0;
        case FilterEncoding::Bytes: {

            const std::size_t per_bank = (kFilterPhases / kFilterBytePhaseStep) * kFilterTaps;
            const FilterIteration iter = word.iteration(FilterTapCount{kFilterTaps});
            const FilterTapIndex tap = word.tap(FilterTapCount{kFilterTaps});
            const FilterPhaseIndex phase = FilterPhaseIndex::at<kFilterBytePhaseStep>(iter);
            const FilterTap t = FilterPhaseIndex::tap_value(tap, src.taps(phase));
            return bank.wire_address(
                       per_bank << kFilterAddrShift,
                       word.wire_offset(FilterAddressGrid{kFilterAddrShift})
                   )
                | t.byte_value();
        }
        case FilterEncoding::Words:
        case FilterEncoding::WordsAdaptive: {

            const bool full = fmt.full_precision;
            const int shift = full ? 0 : 1;
            const std::size_t stride = full ? kFilterPhases * kFilterTaps
                                            : kFilterCoarsePhases * kFilterTaps;
            const FilterEntryWordCount entry_words{kFilterWordsPerEntry};
            const FilterBankWordIndex place = word.pair(entry_words);
            const FilterIteration iter = place.iteration(FilterTapCount{kFilterTaps});
            const FilterTapIndex tap = place.tap(FilterTapCount{kFilterTaps});
            if (word.is_address(entry_words)) {
                return bank.wire_address(stride, place.wire_offset(FilterAddressGrid{}));
            }
            const FilterPhaseIndex phase = full
                ? FilterPhaseIndex::at<1>(iter)
                : FilterPhaseIndex::at<kFilterCoarsePhaseStep>(iter);
            const FilterTap t = FilterPhaseIndex::tap_value(tap, src.taps(phase));
            return t.wide_value(shift);
        }
    }
    return 0;
}

struct NearestNeighbourPhases {
    [[nodiscard]] static constexpr std::array<std::int16_t, kFilterTaps> taps(
        FilterPhaseIndex phase
    ) noexcept {
        constexpr std::size_t kLowHalfHotTap = 1;
        constexpr std::size_t kHighHalfHotTap = 2;
        std::array<std::int16_t, kFilterTaps> t{};
        t[phase.lower_half() ? kLowHalfHotTap : kHighHalfHotTap] = kFilterHotFull;
        return t;
    }
};

}

constexpr std::size_t filter_coeff_word_count(FilterFormat fmt) noexcept {
    return kGeneratedKernelHalves * detail::filter_words_per_bank(fmt);
}

constexpr std::uint16_t filter_coeff_word(FilterFormat fmt, FilterBankWordIndex i) noexcept {
    const std::size_t per_bank = detail::filter_words_per_bank(fmt);
    if (per_bank == 0)
        return 0;
    const FilterBankWordCount bank_words{per_bank};
    return detail::filter_word(
        fmt,
        detail::NearestNeighbourPhases{},
        i.bank(bank_words),
        i.offset(bank_words)
    );
}

}
