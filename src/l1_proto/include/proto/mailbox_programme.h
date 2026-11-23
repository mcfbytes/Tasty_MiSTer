// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "infra/affine.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_select.h"
#include "proto/types.h"
#include "proto/user_io/condition.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto {

inline constexpr FioWordCount kMailboxPollWords{32};
inline constexpr FioWordCount kMailboxGuardWords{2};

void malformed_mailbox_programme();

class MailboxProgramme {
public:
    using Polls = std::span<const MailboxSelect>;
    using Guards = std::span<const user_io::PollFrameShape>;

    consteval explicit MailboxProgramme(Polls polls, Guards guards = {})
        : polls_(polls),
          guards_(guards) {
        if (!well_formed(polls, guards))
            malformed_mailbox_programme();
    }

    [[nodiscard]] static constexpr bool well_formed(Polls polls, Guards guards) noexcept {
        if (polls.empty() || !kMailboxSelects.bounds(polls))
            return false;
        if (!kMailboxGuards.bounds(guards))
            return false;
        MailboxSelectIndex at{};
        for (const MailboxSelect& select : polls) {

            if (select.follows && !(*select.follows < at))
                return false;
            if (select.gate && (!(select.gate->on < at) || !select.gate->mask.any()))
                return false;

            if (select.repeats != ReplyCount{} && !select.gate)
                return false;
            at = at.next();
        }
        if (words_of(polls) > kMailboxPollWords)
            return false;
        if (repeat_words_of(polls) > kMailboxDirectWords)
            return false;
        for (const user_io::PollFrameShape& guard : guards) {

            if (guard.row().guard.condition == user_io::Condition::MaskedChangedSinceLast)
                return false;
            if (guard.max_words() > kMailboxGuardWords)
                return false;
        }
        return true;
    }

    [[nodiscard]] constexpr Polls polls() const noexcept { return polls_; }

    [[nodiscard]] constexpr Guards guards() const noexcept { return guards_; }

    [[nodiscard]] constexpr FioWordCount max_words() const noexcept { return words_of(polls_); }

    [[nodiscard]] static constexpr FioWordCount repeat_words(
        Polls polls,
        const MailboxSelect& select
    ) noexcept {
        if (!select.gate)
            return select.shape.max_words();
        return select.gate->on.in(polls).shape.max_words() + select.shape.max_words();
    }

private:
    [[nodiscard]] static constexpr FioWordCount words_of(Polls polls) noexcept {
        FioWordCount words{};
        for (const MailboxSelect& select : polls)
            words += select.shape.max_words();
        return words;
    }

    [[nodiscard]] static constexpr FioWordCount repeat_words_of(Polls polls) noexcept {
        FioWordCount words{};
        for (const MailboxSelect& select : polls)
            words += repeat_words(polls, select).times(select.repeats);
        return words;
    }

    Polls polls_;
    Guards guards_;
};

}
