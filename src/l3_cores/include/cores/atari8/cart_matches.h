// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cores/atari8/cart_form.h"
#include "cores/atari8/cart_match.h"
#include "cores/atari8/cart_mode.h"
#include "cores/atari8/cart_table.h"
#include "cores/load_choice.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"

namespace mister::cores::atari8 {

class CartMatches {
public:
    static constexpr std::size_t kCapacity = 16;

    bool add(const CartMatch& m) {
        if (matches_.size() == kCapacity)
            return false;
        matches_.push_back(m);
        return true;
    }

    [[nodiscard]] std::span<const CartMatch> all() const noexcept { return matches_; }

    [[nodiscard]] bool empty() const noexcept { return matches_.empty(); }

    [[nodiscard]] Ex<CartMatch> pick(LoadChoice choice) const;

private:
    std::vector<CartMatch> matches_{};
};

inline constexpr proto::FileSize kCarHeadBytes{16};
inline constexpr proto::FileSize kRawHeadBytes{0x2000};

inline constexpr CartMode kSdxSide2Mode{0x4C};
inline constexpr CartMode kSdxU1mbMode{0x4D};

[[nodiscard]] CartMatches match_carts(
    const CartTable& table,
    CartForm form,
    std::span<const std::uint8_t> head,
    proto::FileSize size
);

}
