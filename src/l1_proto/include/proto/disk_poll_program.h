// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "proto/disk_poll_step.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto {

void malformed_disk_poll_program();

class DiskPollProgram {
public:
    static constexpr FioWordCount kMaxWords{64};

    consteval explicit DiskPollProgram(std::span<const DiskPollStep> steps) : steps_(steps) {
        if (!well_formed(steps))
            malformed_disk_poll_program();
    }

    [[nodiscard]] static constexpr bool well_formed(std::span<const DiskPollStep> steps) noexcept {
        if (steps.empty())
            return false;
        const user_io::PollFrameShape& head = steps.front().frame;
        if (head.unguarded() || steps.front().gate.any())
            return false;
        for (const DiskPollStep& step : steps.subspan(1)) {
            if (!step.frame.unguarded() || !step.gate.any())
                return false;
            if (!step.gate.within(head.row().guard.mask))
                return false;
        }
        return kept_of(steps) <= user_io::kMaxPollFrameReplies && words_of(steps) <= kMaxWords;
    }

    [[nodiscard]] constexpr const user_io::PollFrameShape& head() const noexcept {
        return steps_.front().frame;
    }

    [[nodiscard]] constexpr std::span<const DiskPollStep> gated() const noexcept {
        return steps_.subspan(1);
    }

    [[nodiscard]] constexpr ReplyCount kept() const noexcept { return kept_of(steps_); }

    [[nodiscard]] constexpr FioWordCount max_words() const noexcept { return words_of(steps_); }

private:
    [[nodiscard]] static constexpr ReplyCount kept_of(std::span<const DiskPollStep> s) noexcept {
        ReplyCount kept{};
        for (const DiskPollStep& step : s) {

            if (kept > user_io::kMaxPollFrameReplies)
                break;
            kept += step.frame.kept();
        }
        return kept;
    }

    [[nodiscard]] static constexpr FioWordCount words_of(std::span<const DiskPollStep> s) noexcept {
        FioWordCount words{};
        for (const DiskPollStep& step : s)
            words += step.frame.max_words();
        return words;
    }

    std::span<const DiskPollStep> steps_;
};

}
