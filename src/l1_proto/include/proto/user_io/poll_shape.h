// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::proto::user_io {

enum class PollShape : std::uint8_t { InfoPoll, RumblePoll, F12ModPoll, kCount };

enum class Condition : std::uint8_t { NonZero, MaskedChangedSinceLast, kCount };

enum class CommandForm : std::uint8_t { Bare, ParamInHighByte, kCount };

enum class Cadence : std::uint8_t { Census, SessionStart, kCount };

inline constexpr std::uint8_t kMaxPollParams = 6;

struct PollShapeRow {
    PollShape shape;
    std::uint8_t opcode;
    std::uint16_t mask;
    Condition push_when;
    SeatTag sink;
    CommandForm form;
    std::uint8_t params;
    Cadence cadence;
};

inline constexpr auto kPollShapes = std::to_array<PollShapeRow>({
    {PollShape::InfoPoll, 0x36, 0x00FF, Condition::NonZero, SeatTag::Ui, CommandForm::Bare, 1,
     Cadence::Census},
    {PollShape::RumblePoll, 0x3F, 0xFFFF, Condition::MaskedChangedSinceLast, SeatTag::Input,
     CommandForm::ParamInHighByte, kMaxPollParams, Cadence::Census},
    {PollShape::F12ModPoll, 0x43, 0xFFFF, Condition::MaskedChangedSinceLast, SeatTag::Input,
     CommandForm::Bare, 1, Cadence::SessionStart},
});

consteval bool poll_rows_are_ordinal() {
    if (kPollShapes.size() != infra::ordinal(PollShape::kCount)) return false;
    for (std::size_t i = 0; i < kPollShapes.size(); ++i)
        if (infra::ordinal(kPollShapes[i].shape) != i) return false;
    return true;
}
static_assert(poll_rows_are_ordinal(), "kPollShapes is one row per PollShape, in ordinal order");

consteval bool poll_rows_fit_their_form() {
    for (const PollShapeRow& r : kPollShapes) {
        if (r.params == 0 || r.params > kMaxPollParams) return false;
        if (r.form == CommandForm::Bare && r.params != 1) return false;
    }
    return true;
}
static_assert(poll_rows_fit_their_form(), "a Bare row issues one parameter; none exceeds the mask");

[[nodiscard]] constexpr const PollShapeRow& poll_row(PollShape s) noexcept {
    return kPollShapes[infra::ordinal(s)];
}

[[nodiscard]] constexpr std::uint16_t command_word(const PollShapeRow& r,
                                                   std::uint8_t param) noexcept {
    switch (r.form) {
        case CommandForm::Bare:
            return r.opcode;
        case CommandForm::ParamInHighByte:
            return static_cast<std::uint16_t>(r.opcode | (param << 8));
        case CommandForm::kCount:
            break;
    }
    return r.opcode;
}

[[nodiscard]] constexpr bool guard_passes(const PollShapeRow& r, std::uint16_t word,
                                          std::uint16_t last) noexcept {
    switch (r.push_when) {
        case Condition::NonZero:
            return (word & r.mask) != 0;
        case Condition::MaskedChangedSinceLast:
            return (word & r.mask) != (last & r.mask);
        case Condition::kCount:
            break;
    }
    return false;
}

[[nodiscard]] constexpr bool keeps_mirror(const PollShapeRow& r) noexcept {
    return r.push_when == Condition::MaskedChangedSinceLast;
}

}  // namespace mister::proto::user_io
