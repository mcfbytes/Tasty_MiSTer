// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/error.h"
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
    enum class Kind : std::uint8_t {
        Fetch,
        ReadAhead,
        Write,
        Create,
        Flush,
        Attach,
        Detach,
        kCount
    };
    static constexpr std::size_t kStore = 16;

    enum : std::size_t { kStoreAlign = 8 };

    struct Head {
        StorageSeq seq{};
        SlotIndex slot{};
        StorageStatus status = StorageStatus::Ok;
        std::uint8_t pad_[2]{};

        Error fault{};
    };

    struct Fetch {
        static constexpr Kind kKind = Kind::Fetch;
    };

    struct ReadAhead {
        static constexpr Kind kKind = Kind::ReadAhead;
    };

    struct Write {
        static constexpr Kind kKind = Kind::Write;
        FileSize end{};
    };

    struct Create {
        static constexpr Kind kKind = Kind::Create;
        FileSize size{};
        std::uint32_t bytes = 0;
        std::uint8_t pad_[4]{};
    };

    struct Flush {
        static constexpr Kind kKind = Kind::Flush;
    };

    struct Attach {
        static constexpr Kind kKind = Kind::Attach;
        FileSize size{};
    };

    struct Detach {
        static constexpr Kind kKind = Kind::Detach;
    };

    using Alternatives = std::tuple<Fetch, ReadAhead, Write, Create, Flush, Attach, Detach>;

    Kind kind = Kind::Fetch;
    std::uint8_t pad_[7]{};
    Head head{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};
};

static_assert(
    infra::MessageSum<StorageCompletion> && infra::alternatives_are_total<StorageCompletion>()
);
static_assert(sizeof(StorageCompletion) == 40 && alignof(StorageCompletion) == 8);
static_assert(
    sizeof(StorageCompletion::Attach) == 8 && alignof(StorageCompletion::Attach) == 8,
    "an Attach's extent is one 64-bit FileSize"
);

}
