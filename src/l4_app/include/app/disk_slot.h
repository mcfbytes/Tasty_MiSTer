// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "app/disk_attach_seq.h"
#include "app/path_text.h"
#include "app/relay_stamp.h"
#include "cores/disk_servant.h"
#include "cores/staging_core.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/disk_act.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::app {

inline constexpr std::size_t kDiskDepth = 2;

struct DiskSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    struct Ask {
        TASTY_SEAT_EXEMPT(component);
        enum class Kind : std::uint8_t { Bind, Command, Resume, Attach, kCount };

        using Head = RelayStamp;

        struct Bind {
            static constexpr Kind kKind = Kind::Bind;
            proto::DiskUnit unit{};
            std::uint8_t pad_[3]{};
            std::uint32_t seq = 0;
            PathText path{};
        };

        struct Command {
            static constexpr Kind kKind = Kind::Command;
            proto::user_io::PollFrame block{};
        };

        struct Resume {
            static constexpr Kind kKind = Kind::Resume;
        };

        struct Attach {
            static constexpr Kind kKind = Kind::Attach;
            infra::OptRef<cores::IDiskServant> servant{};
            DiskAttachSeq order{};
            std::array<std::byte, 4> pad_{};
        };

        static constexpr std::size_t kStore = (sizeof(Bind) + alignof(Attach) - 1)
            / alignof(Attach)
            * alignof(Attach);

        using Alternatives = std::tuple<Bind, Command, Resume, Attach>;

        Kind kind = Kind::Attach;
        std::uint8_t pad_{};
        Head head{};
        std::uint16_t pad2_{};
        alignas(Bind) alignas(Attach) std::array<std::byte, kStore> store{};
    };

    Ask ask{};

    proto::DiskAct act{};
    cores::MountState bound = cores::MountState::Pending;
    std::array<std::byte, proto::kDiskActBytes> bytes{};
};

static_assert(infra::MessageSum<DiskSlot::Ask> && infra::alternatives_are_total<DiskSlot::Ask>());
static_assert(std::is_trivially_copyable_v<DiskSlot>);

}
