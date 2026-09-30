// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::proto {

struct FioWindow {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Index, Open, Data, Close, IoCommand, kCount };
    static constexpr std::size_t kStore = 4;

    struct Index {
        static constexpr Kind kKind = Kind::Index;
        std::uint16_t index = 0;
    };

    struct Open {
        static constexpr Kind kKind = Kind::Open;
    };

    struct Data {
        static constexpr Kind kKind = Kind::Data;
        std::uint16_t offset = 0;
        std::uint16_t len = 0;
    };

    struct Close {
        static constexpr Kind kKind = Kind::Close;
    };

    struct IoCommand {
        static constexpr Kind kKind = Kind::IoCommand;
        std::uint16_t offset = 0;
        std::uint16_t words = 0;
    };

    using Alternatives = std::tuple<Index, Open, Data, Close, IoCommand>;

    Kind kind = Kind::Close;
    std::uint8_t pad_{};
    alignas(2) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<FioWindow> && infra::alternatives_are_total<FioWindow>());
static_assert(sizeof(FioWindow) == 6, "a queue element stays three words");

}  // namespace mister::proto
