// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "app/config_rel.h"
#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/save_act.h"

namespace mister::app {

inline constexpr std::size_t kSaveUploadDepth = 2;

struct SaveUploadSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    struct Ask {
        TASTY_SEAT_EXEMPT(component);
        enum class Kind : std::uint8_t { Start, Resume, kCount };

        struct Head {
            std::uint16_t gen = 0;
            std::uint16_t epoch = 0;
        };

        struct Start {
            static constexpr Kind kKind = Kind::Start;
            std::uint32_t size = 0;
            std::uint16_t index = 0;
            std::uint8_t word_bytes = 1;
            std::uint8_t pad_{};
        };

        struct Resume {
            static constexpr Kind kKind = Kind::Resume;
        };

        static constexpr std::size_t kStore = sizeof(Start);

        using Alternatives = std::tuple<Start, Resume>;

        Kind kind = Kind::Resume;
        std::uint8_t pad_{};
        Head head{};
        std::uint16_t pad2_{};
        alignas(4) std::array<std::byte, kStore> store{};
    };

    Ask ask{};

    proto::SaveAct act{};
    FixedStr<kConfigRelCap, StrFit::Reject> rel{};
    std::array<std::uint8_t, proto::kSavePieceBytes> bytes{};
};

static_assert(
    infra::MessageSum<SaveUploadSlot::Ask> && infra::alternatives_are_total<SaveUploadSlot::Ask>()
);
static_assert(std::is_trivially_copyable_v<SaveUploadSlot>);

}
