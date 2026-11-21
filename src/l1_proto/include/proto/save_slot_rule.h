// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/slot_attributes.h"
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
        FileOffset offset{};
        std::uint32_t len = 0;
    };

    [[nodiscard]] static constexpr SaveSlotRule at_mount(
        bool exists,
        FileSize extent,
        MountClass mount
    ) noexcept {
        const SlotAttributes attr = SlotAttributes::at_mount(mount, exists);
        return SaveSlotRule{
            .size = exists ? extent : FileSize{},
            .growable = attr.growable,
            .deferred_create = attr.deferred_create
        };
    }

    [[nodiscard]] constexpr Plan plan_write(
        Lba lba,
        BlockSize block_size,
        BlockCount blocks
    ) const noexcept {
        const std::uint32_t sz = block_size.window_bytes(blocks);
        if (deferred_create) {
            if (lba != Lba{})
                return Plan{};
            return Plan{.act = Act::Create, .offset = kFileStart, .len = sz};
        }

        if (sz == 0 || !size.reaches(lba, block_size))
            return Plan{};
        const FileOffset offset = block_size.position_of(lba);
        std::uint32_t len = sz;
        if (!growable) {
            const FileSize rem = offset.remaining_in(size);
            if (rem < FileSize{len})
                len = rem.low_word();
        }
        if (len == 0)
            return Plan{};
        return Plan{.act = Act::Write, .offset = offset, .len = len};
    }

    constexpr void created(FileSize extent) noexcept {
        size = extent;
        deferred_create = false;
    }

    constexpr void wrote(FileOffset at, std::uint32_t len) noexcept {
        if (const FileSize end = at + FileSize{len} - kFileStart; end > size)
            size = end;
    }
};

static_assert(
    SaveSlotRule::at_mount(false, FileSize{}, MountClass::Save)
            .plan_write(Lba{0}, BlockSize{1024}, BlockCount{1})
            .act
        == SaveSlotRule::Act::Create,
    "user_io.cpp:3351: a fresh save is created by the first write to LBA 0"
);
static_assert(
    SaveSlotRule::at_mount(false, FileSize{}, MountClass::Save)
            .plan_write(Lba{1}, BlockSize{1024}, BlockCount{1})
            .act
        == SaveSlotRule::Act::Discard,
    "a write past LBA 0 before the create is dropped"
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{2048}, MountClass::Save)
            .plan_write(Lba{2}, BlockSize{1024}, BlockCount{1})
            .act
        == SaveSlotRule::Act::Write,
    "user_io.cpp:3370: `lba <= size`, one block past the end"
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{2048}, MountClass::Save)
        .plan_write(Lba{3}, BlockSize{1024}, BlockCount{1})
        .act
    == SaveSlotRule::Act::Discard
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{1536}, MountClass::Image)
            .plan_write(Lba{1}, BlockSize{1024}, BlockCount{1})
            .len
        == 512,
    "user_io.cpp:3376-3380: a no-grow image clamps to the bytes remaining"
);
static_assert(
    SaveSlotRule::at_mount(true, FileSize{2048}, MountClass::Save)
        .plan_write(Lba{1}, BlockSize{1024}, BlockCount{1})
        .offset
    == kFileStart + FileSize{1024}
);

}
