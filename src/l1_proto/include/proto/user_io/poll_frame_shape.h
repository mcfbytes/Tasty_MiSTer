// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/types.h"
#include "infra/affine.h"
#include "proto/disk_address.h"
#include "proto/link_sel.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/head_lengths.h"
#include "proto/user_io/poll_frame.h"

namespace mister::proto::user_io {

enum class TransferKind : std::uint8_t { AckedWord, AckedByte, kCount };

inline constexpr FioWordCount kCommandBeatWords{1};
inline constexpr ReplyMask kLowByteLane{0x00FF};

[[nodiscard]] constexpr hal::SpiWord kept_part(TransferKind kind, hal::SpiWord reply) noexcept {
    switch (kind) {
        case TransferKind::AckedWord:
            return reply;
        case TransferKind::AckedByte:
            return hal::SpiWord{reply.low_byte()};
        case TransferKind::kCount:
            break;
    }
    return reply;
}

void malformed_poll_frame_shape();

class PollFrameShape {
public:
    struct Guard {
        Condition condition = Condition::NonZero;
        ReplyIndex reply{};
        ReplyMask mask{};
    };

    struct Row {
        hal::SpiWord opcode{};
        bool keeps_command_reply = false;
        std::optional<DiskAddress> address{};
        TransferKind kind = TransferKind::AckedWord;
        ReplyCount leading{};
        ReplyCount guarded{};
        Guard guard{};
        hal::SpiWord exchanged{};
        LinkSel select = LinkSel::Io;

        std::optional<HeadLengths> head_lengths{};
    };

    consteval explicit PollFrameShape(Row row) : row_(row) {
        if (!well_formed(row))
            malformed_poll_frame_shape();
    }

    [[nodiscard]] static constexpr bool well_formed(const Row& r) noexcept {

        if (r.opcode == hal::SpiWord{} && (r.select == LinkSel::Io || !r.keeps_command_reply))
            return false;
        if (r.kind == TransferKind::kCount || r.guard.condition == Condition::kCount)
            return false;
        if (r.leading > kMaxPollFrameReplies || r.guarded > kMaxPollFrameReplies)
            return false;
        if (kept_of(r) > kMaxPollFrameReplies)
            return false;

        if (r.head_lengths
            && (r.guard.mask.any()
                || r.guarded != ReplyCount{}
                || !infra::affine::in_bounds(kept_before_guard_of(r), r.head_lengths->head)))
            return false;

        if (!r.guard.mask.any())
            return r.guarded == ReplyCount{} && kept_of(r) > ReplyCount{};
        if (!infra::affine::in_bounds(kept_before_guard_of(r), r.guard.reply))
            return false;
        const bool reads_command_reply = r.keeps_command_reply && r.guard.reply == ReplyIndex{};
        return r.kind == TransferKind::AckedWord
            || reads_command_reply
            || r.guard.mask.within(kLowByteLane);
    }

    [[nodiscard]] constexpr const Row& row() const noexcept { return row_; }

    [[nodiscard]] constexpr ReplyCount kept() const noexcept { return kept_of(row_); }

    [[nodiscard]] constexpr ReplyCount kept_before_guard() const noexcept {
        return kept_before_guard_of(row_);
    }

    [[nodiscard]] constexpr bool unguarded() const noexcept { return !row_.guard.mask.any(); }

    [[nodiscard]] constexpr FioWordCount held_words() const noexcept {
        return kCommandBeatWords
            + (row_.address ? DiskAddress::kWords : FioWordCount{})
            + row_.leading.link_words()
            + widest_more_of(row_).link_words();
    }

    [[nodiscard]] constexpr FioWordCount max_words() const noexcept {
        return held_words() + row_.guarded.link_words();
    }

    [[nodiscard]] constexpr bool guard_holds(
        const PollFrame& in_hand,
        std::optional<ReplyWord> last_seen
    ) const noexcept {
        if (unguarded())
            return true;
        return condition_holds(
            row_.guard.condition,
            row_.guard.mask,
            in_hand.reply(row_.guard.reply),
            last_seen
        );
    }

    [[nodiscard]] constexpr ReplyWord guard_word(const PollFrame& taken) const noexcept {
        return taken.reply(row_.guard.reply);
    }

private:
    [[nodiscard]] static constexpr ReplyCount kept_before_guard_of(const Row& r) noexcept {
        return (r.keeps_command_reply ? ReplyCount{1} : ReplyCount{}) + r.leading;
    }

    [[nodiscard]] static constexpr ReplyCount widest_more_of(const Row& r) noexcept {
        return r.head_lengths ? r.head_lengths->widest() : ReplyCount{};
    }

    [[nodiscard]] static constexpr ReplyCount kept_of(const Row& r) noexcept {
        return kept_before_guard_of(r) + widest_more_of(r) + r.guarded;
    }

    Row row_;
};

}
