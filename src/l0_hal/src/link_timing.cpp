// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/link_timing.h"

#include <expected>

#include "hal/fact_field.h"
#include "hal/program_geometry.h"

namespace mister::hal {

Ex<LinkTimingValues> LinkTimingValues::resolve(const LinkTiming& t, std::uint16_t site) noexcept {
    const auto word = t.word_ns.resolve(site, fact_ordinal(FactField::TimingWordNs));
    if (!word) return std::unexpected(word.error());
    const auto beat = t.beat_ns.resolve(site, fact_ordinal(FactField::TimingBeatNs));
    if (!beat) return std::unexpected(beat.error());
    const auto base = t.base_round_ns.resolve(site, fact_ordinal(FactField::TimingBaseRoundNs));
    if (!base) return std::unexpected(base.error());
    const auto sleep = t.sleep_ns.resolve(site, fact_ordinal(FactField::TimingSleepNs));
    if (!sleep) return std::unexpected(sleep.error());
    const auto budget =
        t.round_budget_words.resolve(site, fact_ordinal(FactField::TimingRoundBudgetWords));
    if (!budget) return std::unexpected(budget.error());
    const auto load =
        t.load_budget_words.resolve(site, fact_ordinal(FactField::TimingLoadBudgetWords));
    if (!load) return std::unexpected(load.error());
    const auto spins = t.ack_soft_spins.resolve(site, fact_ordinal(FactField::TimingAckSoftSpins));
    if (!spins) return std::unexpected(spins.error());
    const auto timeout =
        t.ack_timeout_ns.resolve(site, fact_ordinal(FactField::TimingAckTimeoutNs));
    if (!timeout) return std::unexpected(timeout.error());
    return LinkTimingValues{*word, *beat, *base, *sleep, *budget, *load, *spins, *timeout};
}

Ex<ProgramGeometryValues> ProgramGeometryValues::resolve(const ProgramGeometry& g,
                                                         std::uint16_t site) noexcept {
    const auto chunk = g.chunk_bytes.resolve(site, fact_ordinal(FactField::ProgramChunkBytes));
    if (!chunk) return std::unexpected(chunk.error());
    const auto quantum = g.quantum.resolve(site, fact_ordinal(FactField::ProgramQuantum));
    if (!quantum) return std::unexpected(quantum.error());
    const auto align = g.align.resolve(site, fact_ordinal(FactField::ProgramAlign));
    if (!align) return std::unexpected(align.error());
    return ProgramGeometryValues{*chunk, *quantum, *align};
}

}  // namespace mister::hal
