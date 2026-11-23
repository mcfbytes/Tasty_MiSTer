// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "proto/types.h"

namespace mister::svc {

inline constexpr std::byte kErasedFlash{0xFF};

struct SaveBanks {
    proto::FileSize lead{};
    proto::FileSize head{};
    proto::FileSize body{};

    [[nodiscard]] constexpr bool framed() const noexcept { return !body.empty(); }

    [[nodiscard]] constexpr proto::FileSize bank() const noexcept { return head + body; }

    [[nodiscard]] constexpr bool whole_bodies(proto::FileSize wire) const noexcept {
        return !framed() || wire.bytes_past_whole(body).empty();
    }

    [[nodiscard]] constexpr proto::FileSize heads_bytes(proto::FileSize wire) const noexcept {
        return framed() ? head * wire.whole_records(body) : proto::FileSize{};
    }

    [[nodiscard]] constexpr proto::FileSize image_bytes(proto::FileSize wire) const noexcept {
        return framed() ? lead + bank() * wire.whole_records(body) : wire;
    }

    [[nodiscard]] constexpr proto::FileOffset place(proto::FileOffset at) const noexcept {
        if (!framed())
            return at;
        const proto::FileSize wire = at - proto::kFileStart;
        return proto::kFileStart
            + lead
            + bank() * wire.whole_records(body)
            + head
            + wire.bytes_past_whole(body);
    }

    [[nodiscard]] constexpr proto::FileSize run_left(proto::FileOffset at) const noexcept {
        return body - (at - proto::kFileStart).bytes_past_whole(body);
    }

    friend constexpr bool operator==(const SaveBanks&, const SaveBanks&) = default;
};

}
