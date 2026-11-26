// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "hal/types.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto::user_io {

inline constexpr ReplyCount kMaxPollFrameReplies{16};

struct PollFrame {
    TASTY_SEAT_EXEMPT(component);
    using Replies = infra::IndexedArray<hal::LinkWord, kMaxPollFrameReplies.size(), ReplyIndex>;

    Replies replies{};
    ReplyCount kept{};
    std::byte pad_{};

    constexpr void keep(hal::LinkWord reply) noexcept {
        replies[infra::affine::end_of<ReplyIndex>(kept)] = reply;
        kept += ReplyCount{1};
    }

    constexpr void keep_all(const PollFrame& more) noexcept {
        for (ReplyIndex i{}; infra::affine::in_bounds(more.kept, i); i += ReplyCount{1})
            keep(more.replies[i]);
    }

    constexpr void keep_blank(ReplyCount blank) noexcept {
        for (ReplyCount done{}; done < blank; done += ReplyCount{1})
            keep(hal::LinkWord{});
    }

    [[nodiscard]] constexpr ReplyWord reply(ReplyIndex i) const noexcept {
        return ReplyWord::of(replies[i]);
    }

    [[nodiscard]] constexpr infra::ByteCount low_bytes(std::span<std::byte> out) const noexcept {
        std::span<std::byte> rest = out;
        infra::ByteCount done{};
        for (ReplyIndex i{}; infra::affine::in_bounds(kept, i); i += ReplyCount{1}) {
            rest.front() = replies[i].low_lane();
            rest = rest.subspan(1);
            done += infra::ByteCount{1};
        }
        return done;
    }
};

static_assert(std::is_trivially_copyable_v<PollFrame>);
static_assert(std::has_unique_object_representations_v<PollFrame>);

}
