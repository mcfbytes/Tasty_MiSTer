// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/fact.h"

namespace mister::hal {

struct LinkTiming {
    Fact<std::uint32_t> word_ns;
    Fact<std::uint32_t> beat_ns;
    Fact<std::uint32_t> base_round_ns;
    Fact<std::uint32_t> sleep_ns;
    Fact<std::uint32_t> round_budget_words;
    Fact<std::uint32_t> load_budget_words;
    Fact<std::uint32_t> ack_soft_spins;
    Fact<std::uint64_t> ack_timeout_ns;
};

class LinkTimingValues {
    TASTY_SEAT_EXEMPT(component);

public:
    LinkTimingValues() = delete;

    [[nodiscard]] static Ex<LinkTimingValues> resolve(const LinkTiming& t,
                                                      std::uint16_t site) noexcept;

    [[nodiscard]] static consteval LinkTimingValues measured(const LinkTiming& t) {
        return LinkTimingValues{t.word_ns.value(),
                                t.beat_ns.value(),
                                t.base_round_ns.value(),
                                t.sleep_ns.value(),
                                t.round_budget_words.value(),
                                t.load_budget_words.value(),
                                t.ack_soft_spins.value(),
                                t.ack_timeout_ns.value()};
    }

    std::uint32_t word_ns;
    std::uint32_t beat_ns;
    std::uint32_t base_round_ns;
    std::uint32_t sleep_ns;
    std::uint32_t round_budget_words;
    std::uint32_t load_budget_words;
    std::uint32_t ack_soft_spins;
    std::uint64_t ack_timeout_ns;

private:
    constexpr LinkTimingValues(std::uint32_t word, std::uint32_t beat, std::uint32_t base,
                               std::uint32_t sleep, std::uint32_t budget, std::uint32_t load,
                               std::uint32_t spins, std::uint64_t timeout) noexcept
        : word_ns(word), beat_ns(beat), base_round_ns(base), sleep_ns(sleep),
          round_budget_words(budget), load_budget_words(load), ack_soft_spins(spins),
          ack_timeout_ns(timeout) {}
};

constexpr bool all_measured(const LinkTiming& t) noexcept {
    return t.word_ns.measured() && t.beat_ns.measured() && t.base_round_ns.measured() &&
           t.sleep_ns.measured() && t.round_budget_words.measured() &&
           t.load_budget_words.measured() && t.ack_soft_spins.measured() &&
           t.ack_timeout_ns.measured();
}

inline constexpr std::uint64_t kLoadWireNs = 10'000'000;

constexpr std::uint64_t round_cost_ns(std::uint32_t word_ns, std::uint32_t base_round_ns,
                                      std::uint32_t sleep_ns, std::uint32_t words) noexcept {
    return std::uint64_t{words} * word_ns + base_round_ns + sleep_ns;
}

}  // namespace mister::hal
