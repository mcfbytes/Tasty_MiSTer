// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::svc {

template <class Value, std::unsigned_integral Wire, std::endian Order>
struct ImageField {
    proto::FileOffset at{};

    [[nodiscard]] constexpr Value read(std::span<const std::byte> head) const noexcept {
        const std::span<const std::byte, sizeof(Wire)> src = infra::affine::from(head, at)
                                                                 .template first<sizeof(Wire)>();
        if constexpr (Order == std::endian::little) {
            return Value{infra::load_le<Wire>(src)};
        } else {
            return Value{infra::load_be<Wire>(src)};
        }
    }
};

}
