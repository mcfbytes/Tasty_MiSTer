// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hal/types.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::user_io {

enum class PollShape : std::uint8_t { InfoPoll, RumblePoll, F12ModPoll, kCount };

enum class Condition : std::uint8_t { NonZero, MaskedChangedSinceLast, kCount };

enum class CommandForm : std::uint8_t { Bare, ParamInHighByte, kCount };

inline constexpr proto::ParamSet kBareRowParams{0x01};

enum class Cadence : std::uint8_t { Census, SessionStart, kCount };

struct PollShapeRow {
    PollShape shape;
    UioOp opcode;
    hal::SpiWord mask;
    Condition push_when;
    SeatTag sink;
    CommandForm form;
    std::uint8_t params;
    Cadence cadence;
};

inline constexpr auto kPollShapes = std::to_array<PollShapeRow>({
    {.shape = PollShape::InfoPoll,
     .opcode = UioOp::InfoGet,
     .mask = hal::SpiWord{0x00FF},
     .push_when = Condition::NonZero,
     .sink = SeatTag::Ui,
     .form = CommandForm::Bare,
     .params = 1,
     .cadence = Cadence::Census},
    {.shape = PollShape::RumblePoll,
     .opcode = UioOp::GetRumble,
     .mask = hal::SpiWord{0xFFFF},
     .push_when = Condition::MaskedChangedSinceLast,
     .sink = SeatTag::Input,
     .form = CommandForm::ParamInHighByte,
     .params = kMaxPollParams,
     .cadence = Cadence::Census},
    {.shape = PollShape::F12ModPoll,
     .opcode = UioOp::GetF12Mod,
     .mask = hal::SpiWord{0xFFFF},
     .push_when = Condition::MaskedChangedSinceLast,
     .sink = SeatTag::Input,
     .form = CommandForm::Bare,
     .params = 1,
     .cadence = Cadence::SessionStart},
});

consteval bool poll_rows_are_ordinal() {
    if (kPollShapes.size() != infra::ordinal(PollShape::kCount))
        return false;
    for (std::size_t i = 0; i < kPollShapes.size(); ++i)
        if (infra::ordinal(kPollShapes[i].shape) != i)
            return false;
    return true;
}

static_assert(poll_rows_are_ordinal(), "kPollShapes is one row per PollShape, in ordinal order");

consteval bool poll_rows_fit_their_form() {
    for (const PollShapeRow& r : kPollShapes) {
        if (r.params == 0 || r.params > kMaxPollParams)
            return false;
        if (r.form == CommandForm::Bare && r.params != 1)
            return false;
    }
    return true;
}

static_assert(poll_rows_fit_their_form(), "a Bare row issues one parameter; none exceeds the mask");

[[nodiscard]] constexpr const PollShapeRow& poll_row(PollShape s) noexcept {
    return kPollShapes[infra::ordinal(s)];
}

[[nodiscard]] constexpr hal::SpiWord command_word(
    const PollShapeRow& r,
    std::uint8_t param
) noexcept {
    const hal::SpiWord opcode = word(r.opcode);
    switch (r.form) {
        case CommandForm::Bare:
            return opcode;
        case CommandForm::ParamInHighByte:
            return opcode.with_high_byte(param);
        case CommandForm::kCount:
            break;
    }
    return opcode;
}

struct PollReply {
    hal::SpiWord word;
    hal::SpiWord mirror;
};

[[nodiscard]] constexpr bool guard_passes(const PollShapeRow& r, PollReply reply) noexcept {
    switch (r.push_when) {
        case Condition::NonZero:
            return reply.word.any_of(r.mask);
        case Condition::MaskedChangedSinceLast:
            return reply.word.masked(r.mask) != reply.mirror.masked(r.mask);
        case Condition::kCount:
            break;
    }
    return false;
}

[[nodiscard]] constexpr bool keeps_mirror(const PollShapeRow& r) noexcept {
    return r.push_when == Condition::MaskedChangedSinceLast;
}

}
