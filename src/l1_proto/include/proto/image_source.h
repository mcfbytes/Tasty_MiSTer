// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "proto/slot_attributes.h"
#include "proto/types.h"

namespace mister::proto {

class IImageSource {
public:
    virtual ~IImageSource() = default;

    virtual SlotAttributes attributes(SlotIndex slot) const = 0;

    virtual bool fill_blank(SlotIndex, Lba, std::span<std::uint8_t>) { return false; }

    virtual void note_attached(SlotIndex, bool, FileSize) noexcept {}
    virtual void note_detached(SlotIndex) noexcept {}

    virtual std::uint8_t request_tag(SlotIndex) const noexcept { return 0; }

protected:
    IImageSource() = default;
    IImageSource(const IImageSource&) = default;
    IImageSource& operator=(const IImageSource&) = default;
};

}  // namespace mister::proto
