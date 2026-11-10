// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/mailbox_words.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

inline constexpr std::size_t kMailboxActBytes = 1024;

inline constexpr std::size_t kMailboxCommandWords = 3;

struct MailboxAct {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { None, Command, Download, kCount };

    struct None {
        static constexpr Kind kKind = Kind::None;
    };

    struct Command {
        static constexpr Kind kKind = Kind::Command;
        UioOp opcode{};
        MailboxWords words{};
    };

    struct Download {
        static constexpr Kind kKind = Kind::Download;
        WideIoIndex index{};
        std::uint16_t len = 0;
    };

    static constexpr std::size_t kStore = sizeof(Command);
    static_assert(sizeof(Command) >= sizeof(Download), "the store holds every alternative");

    using Alternatives = std::tuple<None, Command, Download>;

    Kind kind = Kind::None;
    std::uint8_t pad_{};
    alignas(alignof(Command)) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<MailboxAct> && infra::alternatives_are_total<MailboxAct>());
static_assert(std::is_trivially_copyable_v<MailboxAct>);

}
