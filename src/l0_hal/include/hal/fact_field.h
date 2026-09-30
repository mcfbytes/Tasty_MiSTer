// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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

inline constexpr std::size_t kFactFieldCount =
    static_cast<std::size_t>(FactField::TimingLoadBudgetWords) + 1;

constexpr std::uint32_t fact_ordinal(FactField f) noexcept { return static_cast<std::uint32_t>(f); }

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

}  // namespace mister::hal
