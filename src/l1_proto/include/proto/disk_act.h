// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::size_t kDiskStateBytes = 16;

inline constexpr std::size_t kDiskActBytes = 512;

struct DiskAct {

    enum class Kind : std::uint8_t { None, Out, In, Signal };

    static constexpr std::uint32_t kWordBytes = hal::word_bytes(hal::Width::Word);

    [[nodiscard]] static constexpr DiskAct none() noexcept { return DiskAct{}; }

    [[nodiscard]] static constexpr DiskAct out(hal::SpiWord opcode, std::uint16_t len) noexcept {
        DiskAct act;
        act.kind_ = Kind::Out;
        act.last_ = false;
        act.opcode_ = opcode;
        act.len_ = len;
        return act;
    }

    [[nodiscard]] static constexpr DiskAct in(hal::SpiWord opcode, std::uint16_t len) noexcept {
        DiskAct act;
        act.kind_ = Kind::In;
        act.last_ = false;
        act.opcode_ = opcode;
        act.len_ = len;
        return act;
    }

    [[nodiscard]] static constexpr DiskAct signal(
        hal::SpiWord opcode,
        bool last,
        std::optional<hal::SpiWord> word
    ) noexcept {
        DiskAct act;
        act.kind_ = Kind::Signal;
        act.last_ = last;
        act.opcode_ = opcode;
        if (word) {
            act.has_word_ = true;
            act.word_ = *word;
        }
        return act;
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr bool last() const noexcept { return last_; }

    [[nodiscard]] constexpr hal::SpiWord opcode() const noexcept { return opcode_; }

    [[nodiscard]] constexpr std::uint16_t len() const noexcept { return len_; }

    [[nodiscard]] constexpr FioWordCount words() const noexcept {
        return FioWordCount{1u + len_ / kWordBytes + (has_word_ ? 1u : 0u)};
    }

    [[nodiscard]] constexpr std::optional<hal::SpiWord> word() const noexcept {
        if (!has_word_)
            return std::nullopt;
        return word_;
    }

    [[nodiscard]] constexpr std::optional<DiskUnit> unit() const noexcept {
        if (unit_ == kNoUnit_)
            return std::nullopt;
        return unit_;
    }

    [[nodiscard]] constexpr DiskAct with_unit(DiskUnit unit) const noexcept {
        if (unit == kNoUnit_) {
            fatal(unit.refusal(Errc::slot_range, ERR_SITE()), "DiskAct::with_unit: not a unit");
        }
        DiskAct act = *this;
        act.unit_ = unit;
        return act;
    }

private:
    static constexpr DiskUnit kNoUnit_{0xFF};

    Kind kind_ = Kind::None;
    bool last_ = true;
    bool has_word_ = false;
    DiskUnit unit_ = kNoUnit_;
    hal::SpiWord opcode_{};
    std::uint16_t len_ = 0;
    hal::SpiWord word_{};
};

static_assert(std::is_trivially_copyable_v<DiskAct>);
static_assert(sizeof(DiskAct) == 10u, "the loan slot's payload, unchanged");

}
