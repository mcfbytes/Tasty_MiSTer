// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::svc {

struct PadSignature {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr PadSignature none() noexcept { return PadSignature{}; }

    [[nodiscard]] static PadSignature of(std::string_view key) noexcept;

    [[nodiscard]] PadSignature folded(std::string_view next_key) const noexcept;

    [[nodiscard]] constexpr bool is_bound() const noexcept { return v != 0; }

    friend constexpr bool operator==(PadSignature, PadSignature) = default;
};

}
