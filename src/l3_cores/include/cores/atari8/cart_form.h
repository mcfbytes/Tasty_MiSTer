// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::cores::atari8 {

enum class CartForm : std::uint8_t { Car, Raw };

[[nodiscard]] constexpr CartForm cart_form_of(proto::WideIoIndex index) noexcept {
    return index.sub() == proto::WideSub{} ? CartForm::Car : CartForm::Raw;
}

inline constexpr proto::FileSize kCarHeaderBytes{16};

}
