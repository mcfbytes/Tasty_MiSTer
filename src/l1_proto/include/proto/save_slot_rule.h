// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

struct SaveSlotRule {
    TASTY_SEAT_EXEMPT(component);

    FileSize size{};
    bool growable = false;
    bool deferred_create = false;

    enum class Act : std::uint8_t { Discard, Create, Write };

    struct Plan {
        Act act = Act::Discard;
        std::uint64_t offset = 0;
        std::uint32_t len = 0;
    };

    [[nodiscard]] static constexpr SaveSlotRule at_mount(
        bool exists,
        FileSize extent,
        bool pre
    ) noexcept {
        return SaveSlotRule{
            .size = exists ? extent : FileSize{},
            .growable = pre,
            .deferred_create = pre && !exists
        };
    }

    [[nodiscard]] constexpr Plan plan_write(
        Lba lba,
        std::uint32_t block_size,
        std::uint32_t sz
    ) const noexcept {
        if (deferred_create) {
            if (lba.v != 0)
                return Plan{};
            return Plan{.act = Act::Create, .offset = 0, .len = sz};
        }

        const std::uint64_t size_in_blocks = block_size == 0 ? 0 : size.v / block_size;
        if (sz == 0 || static_cast<std::uint64_t>(lba.v) > size_in_blocks)
            return Plan{};
        const std::uint64_t offset = static_cast<std::uint64_t>(lba.v) * block_size;
        std::uint32_t len = sz;
        if (!growable) {
            const std::uint64_t rem = size.v > offset ? size.v - offset : 0;
            if (rem < len)
                len = static_cast<std::uint32_t>(rem);
        }
        if (len == 0)
            return Plan{};
        return Plan{.act = Act::Write, .offset = offset, .len = len};
    }

    constexpr void created(FileSize extent) noexcept {
        size = extent;
        deferred_create = false;
    }

    constexpr void wrote(std::uint64_t offset, std::uint32_t len) noexcept {
        if (const std::uint64_t end = offset + len; end > size.v)
            size = FileSize{end};
    }
};

static_assert(
    SaveSlotRule::at_mount(false, FileSize{}, true).plan_write(Lba{0}, 1024, 1024).act
        == SaveSlotRule::Act::Create,
    "user_io.cpp:3351: a fresh save is created by the first write to LBA 0"
);
static_assert(
    SaveSlotRule::at_mount(false, FileSize{}, true).plan_write(Lba{1}, 1024, 1024).act
        == SaveSlotRule::Act::Discard,
    "a write past LBA 0 before the create is dropped"
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{2048}, true).plan_write(Lba{2}, 1024, 1024).act
        == SaveSlotRule::Act::Write,
    "user_io.cpp:3370: `lba <= size`, one block past the end"
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{2048}, true).plan_write(Lba{3}, 1024, 1024).act
    == SaveSlotRule::Act::Discard
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{1536}, false).plan_write(Lba{1}, 1024, 1024).len == 512,
    "user_io.cpp:3376-3380: a no-grow image clamps to the bytes remaining"
);

}
