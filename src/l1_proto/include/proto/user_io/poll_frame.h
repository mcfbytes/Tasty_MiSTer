// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "hal/types.h"
#include "infra/affine.h"
#include "infra/indexed_array.h"
#include "proto/types.h"

namespace mister::proto::user_io {

inline constexpr ReplyCount kMaxPollFrameReplies{16};

struct PollFrame {
    using Replies = infra::IndexedArray<hal::SpiWord, kMaxPollFrameReplies.size(), ReplyIndex>;

    Replies replies{};
    ReplyCount kept{};

    constexpr void keep(hal::SpiWord reply) noexcept {
        replies[infra::affine::end_of<ReplyIndex>(kept)] = reply;
        kept += ReplyCount{1};
    }

    [[nodiscard]] constexpr ReplyWord reply(ReplyIndex i) const noexcept {
        return ReplyWord::of(replies[i]);
    }
};

static_assert(std::is_trivially_copyable_v<PollFrame>);

}
