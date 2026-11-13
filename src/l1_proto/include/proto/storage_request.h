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

struct StorageRequest {
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
        ImageTag tag{};
        std::uint8_t pad_[2]{};
    };

    struct Fetch {
        static constexpr Kind kKind = Kind::Fetch;
        Lba base{};
        BlockSize block_size{};
    };

    struct ReadAhead {
        static constexpr Kind kKind = Kind::ReadAhead;
        Lba base{};
        BlockSize block_size{};
    };

    struct Write {
        static constexpr Kind kKind = Kind::Write;
        std::uint64_t offset = 0;
        std::uint32_t bytes = 0;
        std::uint8_t pad_[4]{};
    };

    struct Create {
        static constexpr Kind kKind = Kind::Create;
        std::uint32_t bytes = 0;
        std::uint8_t pad_[4]{};
    };

    struct Flush {
        static constexpr Kind kKind = Kind::Flush;
    };

    struct Attach {
        static constexpr Kind kKind = Kind::Attach;
        bool growable = false;
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

static_assert(infra::MessageSum<StorageRequest> && infra::alternatives_are_total<StorageRequest>());
static_assert(sizeof(StorageRequest) == 32 && alignof(StorageRequest) == 8);

}
