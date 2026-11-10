// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <tuple>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/fact.h"
#include "hal/fact_field.h"

namespace mister::hal {

struct LinkTiming {
    Fact<std::chrono::nanoseconds> word_ns;
    Fact<std::chrono::nanoseconds> beat_ns;
    Fact<std::chrono::nanoseconds> base_round_ns;
    Fact<std::chrono::nanoseconds> sleep_ns;
    Fact<std::uint32_t> round_budget_words;
    Fact<std::uint32_t> load_budget_words;
    Fact<std::uint32_t> ack_soft_spins;
    Fact<std::chrono::nanoseconds> ack_timeout_ns;
};

class LinkTimingValues {

public:
    [[nodiscard]] static Ex<LinkTimingValues> resolve(
        const LinkTiming& t,
        infra::ErrorSite site
    ) noexcept;

    [[nodiscard]] static consteval LinkTimingValues measured(const LinkTiming& t) noexcept;

    [[nodiscard]] static constexpr const auto& rows() noexcept { return kFields; }

    std::chrono::nanoseconds word_ns;
    std::chrono::nanoseconds beat_ns;
    std::chrono::nanoseconds base_round_ns;
    std::chrono::nanoseconds sleep_ns;
    std::uint32_t round_budget_words;
    std::uint32_t load_budget_words;
    std::uint32_t ack_soft_spins;
    std::chrono::nanoseconds ack_timeout_ns;

    [[nodiscard]] constexpr std::chrono::nanoseconds round_cost(
        std::uint32_t words
    ) const noexcept {
        return word_ns * words + base_round_ns + sleep_ns;
    }

private:
    static constexpr auto kFields = std::tuple{
        fact_row(FactField::TimingWordNs, &LinkTiming::word_ns, &LinkTimingValues::word_ns),
        fact_row(FactField::TimingBeatNs, &LinkTiming::beat_ns, &LinkTimingValues::beat_ns),
        fact_row(
            FactField::TimingBaseRoundNs,
            &LinkTiming::base_round_ns,
            &LinkTimingValues::base_round_ns
        ),
        fact_row(FactField::TimingSleepNs, &LinkTiming::sleep_ns, &LinkTimingValues::sleep_ns),
        fact_row(
            FactField::TimingRoundBudgetWords,
            &LinkTiming::round_budget_words,
            &LinkTimingValues::round_budget_words
        ),
        fact_row(
            FactField::TimingLoadBudgetWords,
            &LinkTiming::load_budget_words,
            &LinkTimingValues::load_budget_words
        ),
        fact_row(
            FactField::TimingAckSoftSpins,
            &LinkTiming::ack_soft_spins,
            &LinkTimingValues::ack_soft_spins
        ),
        fact_row(
            FactField::TimingAckTimeoutNs,
            &LinkTiming::ack_timeout_ns,
            &LinkTimingValues::ack_timeout_ns
        ),
    };

    constexpr LinkTimingValues() noexcept = default;
};

static_assert(
    rows_are_distinct(LinkTimingValues::rows(), "timing."),
    "each LinkTiming row names one timing FactField, and no field twice"
);

consteval LinkTimingValues LinkTimingValues::measured(const LinkTiming& t) noexcept {
    LinkTimingValues values{};
    fill_rows_measured(values, kFields, t);
    return values;
}

[[nodiscard]] constexpr bool all_measured(const LinkTiming& t) noexcept {
    return every_row_measured(LinkTimingValues::rows(), t);
}

inline constexpr std::chrono::nanoseconds kLoadWire{std::chrono::milliseconds{10}};

}
