// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

enum class StorageStatus : std::uint8_t {
    Ok,
    Io,
    NotFound,
    ShortRead,
    ShortWrite,
    BadFormat,
    Refused,
};

struct StorageCompletion {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Read, Write, Create, Flush, Attach, Detach, kCount };
    static constexpr std::size_t kStore = 16;

    struct Head {
        StorageSeq seq{};
        SlotIndex slot{};
        StorageStatus status = StorageStatus::Ok;
        std::uint8_t pad_[2]{};
    };

    struct Read {
        static constexpr Kind kKind = Kind::Read;
        std::uint64_t offset = 0;
        std::uint32_t bytes = 0;
        ArenaHalf half{};
        std::uint8_t pad_[3]{};
    };
    struct Write {
        static constexpr Kind kKind = Kind::Write;
        std::uint64_t offset = 0;
        std::uint32_t bytes = 0;
        ArenaHalf half{};
        std::uint8_t pad_[3]{};
    };
    struct Create {
        static constexpr Kind kKind = Kind::Create;
        std::uint64_t size = 0;
        std::uint32_t bytes = 0;
        ArenaHalf half{};
        std::uint8_t pad_[3]{};
    };
    struct Flush {
        static constexpr Kind kKind = Kind::Flush;
    };
    struct Attach {
        static constexpr Kind kKind = Kind::Attach;
        std::uint64_t size = 0;
    };
    struct Detach {
        static constexpr Kind kKind = Kind::Detach;
    };

    using Alternatives = std::tuple<Read, Write, Create, Flush, Attach, Detach>;

    Kind kind = Kind::Read;
    std::uint8_t pad_[7]{};
    Head head{};
    alignas(8) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<StorageCompletion> &&
              infra::alternatives_are_total<StorageCompletion>());
static_assert(sizeof(StorageCompletion) == 32 && alignof(StorageCompletion) == 8);

}  // namespace mister::proto
