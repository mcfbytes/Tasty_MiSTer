// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/types.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link_sel.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto::user_io {

enum class PollShape : std::uint8_t {
    InfoPoll,
    RumblePoll,
    F12ModPoll,
    KbdController,
    ChangeCounter,
    kCount,
};

enum class CommandForm : std::uint8_t { Bare, ParamInHighByte, kCount };

inline constexpr proto::ParamSet kBareRowParams{0x01};

enum class Cadence : std::uint8_t { Census, SessionStart, kCount };

struct PollShapeRow {
    PollShape shape;

    std::optional<UioOp> opcode;
    ReplyMask mask;
    Condition push_when;
    SeatTag sink;
    CommandForm form;
    std::uint8_t params;
    Cadence cadence;

    LinkSel select = LinkSel::Io;

    std::optional<PollFrameShape> frame{};
};

inline constexpr auto kPollShapes = std::to_array<PollShapeRow>({
    {.shape = PollShape::InfoPoll,
     .opcode = UioOp::InfoGet,
     .mask = ReplyMask{0x00FF},
     .push_when = Condition::NonZero,
     .sink = SeatTag::Ui,
     .form = CommandForm::Bare,
     .params = 1,
     .cadence = Cadence::Census},
    {.shape = PollShape::RumblePoll,
     .opcode = UioOp::GetRumble,
     .mask = ReplyMask{0xFFFF},
     .push_when = Condition::MaskedChangedSinceLast,
     .sink = SeatTag::Input,
     .form = CommandForm::ParamInHighByte,
     .params = kMaxPollParams,
     .cadence = Cadence::Census},
    {.shape = PollShape::F12ModPoll,
     .opcode = UioOp::GetF12Mod,
     .mask = ReplyMask{0xFFFF},
     .push_when = Condition::MaskedChangedSinceLast,
     .sink = SeatTag::Input,
     .form = CommandForm::Bare,
     .params = 1,
     .cadence = Cadence::SessionStart},
    {.shape = PollShape::KbdController,
     .opcode = UioOp::Mouse,
     .mask = ReplyMask{0x0001},
     .push_when = Condition::NonZero,
     .sink = SeatTag::Input,
     .form = CommandForm::Bare,
     .params = 1,
     .cadence = Cadence::Census,
     .select = LinkSel::Io,
     .frame = PollFrameShape{PollFrameShape::Row{
         .opcode = word(UioOp::Mouse),
         .kind = TransferKind::AckedByte,
         .leading = ReplyCount{1},
         .guarded = ReplyCount{1},
         .guard = {.condition = Condition::NonZero, .mask = ReplyMask{0x0001}}
     }}},
    {.shape = PollShape::ChangeCounter,
     .opcode = std::nullopt,
     .mask = ReplyMask{0x00FF},
     .push_when = Condition::MaskedChangedSinceLast,
     .sink = SeatTag::Main,
     .form = CommandForm::Bare,
     .params = 1,
     .cadence = Cadence::Census,
     .select = LinkSel::Fpga},
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

consteval bool poll_rows_fit_their_select() {
    for (const PollShapeRow& r : kPollShapes) {
        if (r.select != LinkSel::Io && r.select != LinkSel::Fpga)
            return false;
        if (r.opcode.has_value() != (r.select == LinkSel::Io))
            return false;
        if (!r.opcode && (r.frame || r.form != CommandForm::Bare))
            return false;
        if (!r.frame)
            continue;
        const PollFrameShape::Row& f = r.frame->row();
        if (r.form != CommandForm::Bare || r.cadence != Cadence::Census)
            return false;
        if (f.opcode != word(*r.opcode) || f.guard.mask != r.mask)
            return false;
        if (f.guard.condition != r.push_when || r.push_when != Condition::NonZero)
            return false;
    }
    return true;
}

static_assert(
    poll_rows_fit_their_select(),
    "a framed row and its frame agree; a row names an opcode exactly on the IO select"
);

[[nodiscard]] constexpr const PollShapeRow& poll_row(PollShape s) noexcept {
    return kPollShapes[infra::ordinal(s)];
}

inline constexpr hal::LinkWord kInBandRead{};

[[nodiscard]] constexpr hal::LinkWord command_word(
    const PollShapeRow& r,
    std::uint8_t param
) noexcept {
    if (!r.opcode)
        return kInBandRead;
    const hal::LinkWord opcode = word(*r.opcode);
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
    ReplyWord word;
    ReplyWord mirror;
};

[[nodiscard]] constexpr bool guard_passes(const PollShapeRow& r, PollReply reply) noexcept {
    return condition_holds(r.push_when, r.mask, reply.word, reply.mirror);
}

[[nodiscard]] constexpr bool keeps_mirror(const PollShapeRow& r) noexcept {
    return r.push_when == Condition::MaskedChangedSinceLast;
}

}
