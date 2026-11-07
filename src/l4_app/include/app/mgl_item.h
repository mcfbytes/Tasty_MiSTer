// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>

#include "app/mgl_delay.h"
#include "app/path_text.h"
#include "app/remembered_path.h"
#include "app/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::app {

struct MglItemNo {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr MglItemNo of(std::size_t i) noexcept {
        return MglItemNo{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    [[nodiscard]] constexpr MglItemNo next() const noexcept {
        return MglItemNo{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr bool below(std::size_t count) const noexcept { return v < count; }

    friend constexpr bool operator==(MglItemNo, MglItemNo) = default;
    friend constexpr auto operator<=>(MglItemNo, MglItemNo) = default;
};

inline constexpr proto::FileSlotDigit kMglIndexMax{15};

[[nodiscard]] constexpr bool mgl_index_in_range(proto::FileSlotDigit d) noexcept {
    return d <= kMglIndexMax;
}

[[nodiscard]] constexpr proto::IoIndex mount_index_of(proto::FileSlotDigit d) noexcept {
    return d.io_index();
}

static_assert(
    DelaySeconds::of(70000) == DelaySeconds{65535},
    "a `delay=` past the field's 16 bits saturates, as strtoul's answer did"
);
static_assert(
    mgl_index_in_range(proto::FileSlotDigit{15}) && !mgl_index_in_range(proto::FileSlotDigit{16}),
    "the CONF_STR selector's 4-bit namespace: 15 is the last index an item may name"
);

struct MglItem {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Load, Reset, kCount };
    enum class Slot : std::uint8_t { File, Image };

    static constexpr std::size_t kPathMax = infra::kPathMax;
    static constexpr std::size_t kStore = kPathMax + 8;

    struct Load {
        static constexpr Kind kKind = Kind::Load;
        Slot slot = Slot::File;
        proto::FileSlotDigit index{};
        DelaySeconds delay_s{};

        FixedStr<kPathMax, StrFit::Clip> path{};
    };

    struct Reset {
        static constexpr Kind kKind = Kind::Reset;
        DelaySeconds delay_s{};
        std::uint8_t hold_s = 0;
        std::uint8_t pad_ = 0;
    };

    using Alternatives = std::tuple<Load, Reset>;

    Kind kind = Kind::Load;
    std::uint8_t pad_ = 0;
    alignas(std::uint16_t) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<MglItem> && infra::alternatives_are_total<MglItem>());
static_assert(sizeof(MglItem) == 2 + MglItem::kStore, "kind, pad and store");

}
