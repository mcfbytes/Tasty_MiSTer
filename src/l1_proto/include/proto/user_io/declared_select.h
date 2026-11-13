// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/message_sum.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::user_io {

inline constexpr ReplyMask kBoostCapability{0x0002};

inline constexpr ReplyMask kAnyReplyBit{0xFFFF};

enum class SelectRow : std::uint8_t { AfilterLead, AudvolByFilter, kCount };

struct SelectRowFacts {
    SelectRow kind;
    UioOp reply_of;
    ReplyMask mask;
};

inline constexpr auto kDeclaredSelects = std::to_array<SelectRowFacts>({
    {SelectRow::AfilterLead, UioOp::SetAfilter, kBoostCapability},
    {SelectRow::AudvolByFilter, UioOp::SetAfilter, kAnyReplyBit},
});
static_assert(infra::rows_are_ordinal(kDeclaredSelects));
static_assert(kDeclaredSelects.size() == 2, "rt-rule: each select row is an operator ruling");

template <SelectRow R, class T>
    requires std::is_trivially_copyable_v<T>
struct DeclaredSelect {
    T if_set{};
    T if_clear{};

    [[nodiscard]] constexpr T pick(ReplyWord r) const noexcept {
        return r.any(kDeclaredSelects[infra::ordinal(R)].mask) ? if_set : if_clear;
    }
};

}
