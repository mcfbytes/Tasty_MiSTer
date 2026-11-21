// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"

namespace mister::svc::disc {

struct SeekSpan {
    proto::Lba from{};
    proto::Lba to{};

    [[nodiscard]] constexpr bool within(proto::Lba limit) const noexcept {
        return from <= limit && to <= limit;
    }

    [[nodiscard]] constexpr proto::BlockCount distance() const noexcept {
        return infra::affine::distance(from, to);
    }
};

static_assert(
    sizeof(SeekSpan) == 2 * sizeof(proto::Lba),
    "the seek parameter travels as two register-sized members"
);
static_assert(
    SeekSpan{proto::Lba{10}, proto::Lba{4}}.distance() == proto::BlockCount{6}
        && SeekSpan{proto::Lba{4}, proto::Lba{10}}.distance() == proto::BlockCount{6},
    "the distance is taken in the order that is non-negative"
);

}
