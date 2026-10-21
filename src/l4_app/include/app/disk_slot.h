// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "app/path_text.h"
#include "cores/staging_core.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/disk_act.h"

namespace mister::app {

inline constexpr std::size_t kDiskDepth = 2;

struct DiskSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    struct Ask {
        TASTY_SEAT_EXEMPT(component);
        enum class Kind : std::uint8_t { Bind, Command, Resume, Release, kCount };

        struct Head {
            std::uint16_t gen = 0;
            std::uint16_t epoch = 0;
        };

        struct Bind {
            static constexpr Kind kKind = Kind::Bind;
            std::uint8_t unit = 0;
            std::uint8_t pad_[3]{};
            std::uint32_t seq = 0;
            PathText path{};
        };

        struct Command {
            static constexpr Kind kKind = Kind::Command;
            std::array<std::byte, proto::kDiskStateBytes> state{};
        };

        struct Resume {
            static constexpr Kind kKind = Kind::Resume;
        };

        struct Release {
            static constexpr Kind kKind = Kind::Release;
        };

        static constexpr std::size_t kStore = sizeof(Bind);

        using Alternatives = std::tuple<Bind, Command, Resume, Release>;

        Kind kind = Kind::Release;
        std::uint8_t pad_{};
        Head head{};
        std::uint16_t pad2_{};
        alignas(4) std::array<std::byte, kStore> store{};
    };

    Ask ask{};

    proto::DiskAct act{};
    cores::MountState bound = cores::MountState::Pending;
    std::array<std::byte, proto::kDiskActBytes> bytes{};
};

static_assert(infra::MessageSum<DiskSlot::Ask> && infra::alternatives_are_total<DiskSlot::Ask>());
static_assert(std::is_trivially_copyable_v<DiskSlot>);

}
