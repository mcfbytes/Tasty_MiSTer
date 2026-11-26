// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/ocram_types.h"
#include "hal/reg_field.h"

namespace mister::hal::ocram {

class ControlPage;

class SectorShape {
public:
    static constexpr Words kMaxFrame{4096};

    [[nodiscard]] constexpr Words frame() const noexcept { return frame_; }

    template <class Slot>
    [[nodiscard]] constexpr bool names(Slot slot) const noexcept {
        return slot.index() == slot_;
    }

    friend constexpr bool operator==(SectorShape, SectorShape) = default;

private:
    friend class ControlPage;

    [[nodiscard]] static constexpr bool one_descriptor(std::uint32_t k2) noexcept {
        return kDescriptors.get(k2) == 1u;
    }

    [[nodiscard]] static constexpr std::optional<SectorShape> of(std::uint32_t k4) noexcept {
        if (!kReadFlag.test(k4))
            return std::nullopt;
        const auto frame = static_cast<std::uint16_t>(kFrameField.get(k4));
        if (frame == 0u || kMaxFrame < Words{frame})
            return std::nullopt;
        return SectorShape{static_cast<std::uint8_t>(kSlotField.get(k4)), Words{frame}};
    }

    static constexpr RegField<std::uint32_t> kDescriptors{0, 0xFFFF};
    static constexpr RegField<std::uint32_t> kReadFlag{24, 1};
    static constexpr RegField<std::uint32_t> kSlotField{16, 0xF};
    static constexpr RegField<std::uint32_t> kFrameField{0, 0x1FFF};

    constexpr SectorShape(std::uint8_t slot, Words frame) noexcept : slot_(slot), frame_(frame) {}

    std::uint8_t slot_ = 0;
    Words frame_{};
};

}
