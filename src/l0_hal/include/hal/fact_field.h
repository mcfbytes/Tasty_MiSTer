// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>

#include "hal/fact.h"

namespace mister::hal {

enum class FactField : std::uint8_t {
    MailboxGpoOffset,
    MailboxGpiOffset,
    VideoI2cBuses,
    TimingWordNs,
    TimingBeatNs,
    TimingBaseRoundNs,
    TimingSleepNs,
    TimingRoundBudgetWords,
    TimingAckSoftSpins,
    TimingAckTimeoutNs,
    MemoryFpgaDdr,
    MemoryWriteCombine,
    ProgramChunkBytes,
    ProgramQuantum,
    ProgramAlign,
    TimingLoadBudgetWords,
};

inline constexpr std::size_t kFactFieldCount = static_cast<std::size_t>(
                                                   FactField::TimingLoadBudgetWords
                                               )
    + 1;

constexpr std::uint32_t fact_ordinal(FactField f) noexcept {
    return static_cast<std::uint32_t>(f);
}

constexpr std::string_view fact_field_name(FactField f) noexcept {
    switch (f) {
        case FactField::MailboxGpoOffset:
            return "mailbox_gpo_offset";
        case FactField::MailboxGpiOffset:
            return "mailbox_gpi_offset";
        case FactField::VideoI2cBuses:
            return "video.i2c_buses";
        case FactField::TimingWordNs:
            return "timing.word_ns";
        case FactField::TimingBeatNs:
            return "timing.beat_ns";
        case FactField::TimingBaseRoundNs:
            return "timing.base_round_ns";
        case FactField::TimingSleepNs:
            return "timing.sleep_ns";
        case FactField::TimingRoundBudgetWords:
            return "timing.round_budget_words";
        case FactField::TimingAckSoftSpins:
            return "timing.ack_soft_spins";
        case FactField::TimingAckTimeoutNs:
            return "timing.ack_timeout_ns";
        case FactField::MemoryFpgaDdr:
            return "memory.fpga_ddr";
        case FactField::MemoryWriteCombine:
            return "memory.write_combine";
        case FactField::ProgramChunkBytes:
            return "program.chunk_bytes";
        case FactField::ProgramQuantum:
            return "program.quantum";
        case FactField::ProgramAlign:
            return "program.align";
        case FactField::TimingLoadBudgetWords:
            return "timing.load_budget_words";
    }
    return "?";
}

template <class Group, class Values, class Rep>
struct FactRow {
    FactField field;
    Fact<Rep> Group::* source;
    Rep Values::* target;

    [[nodiscard]] constexpr bool measured(const Group& group) const noexcept {
        return (group.*source).measured();
    }

    [[nodiscard]] constexpr Ex<Rep> resolve(
        const Group& group,
        infra::ErrorSite site
    ) const noexcept {
        return (group.*source).resolve(site, fact_ordinal(field));
    }

    constexpr void fill(Values& values, Rep value) const noexcept { values.*target = value; }

    consteval void fill_constant(Values& values, const Group& group) const {
        values.*target = (group.*source).value();
    }
};

template <class Group, class Values, class Rep>
[[nodiscard]] constexpr FactRow<Group, Values, Rep> fact_row(
    FactField field,
    Fact<Rep> Group::* source,
    Rep Values::* target
) noexcept {
    return {field, source, target};
}

template <class Group>
[[nodiscard]] constexpr bool every_row_measured(const auto& rows, const Group& group) noexcept {
    return std::apply([&](const auto&... row) { return (row.measured(group) && ...); }, rows);
}

template <class Values, class Group>
consteval void fill_rows_measured(Values& values, const auto& rows, const Group& group) {
    std::apply(
        [&](const auto&... row) consteval { (row.fill_constant(values, group), ...); },
        rows
    );
}

[[nodiscard]] consteval bool rows_are_distinct(const auto& rows, std::string_view group) {
    bool seen[kFactFieldCount]{};
    bool distinct = true;
    const auto mark = [&](auto row) {
        const auto f = row.field;
        const bool of_group = fact_field_name(f).starts_with(group);
        distinct = distinct && of_group && !seen[fact_ordinal(f)];
        seen[fact_ordinal(f)] = true;
    };
    std::apply([&](const auto&... row) { (mark(row), ...); }, rows);
    return distinct;
}

}
