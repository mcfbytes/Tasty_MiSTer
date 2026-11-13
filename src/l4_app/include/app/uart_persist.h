// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/uart_modes.h"
#include "cores/types.h"

namespace mister::app {

struct UartPersist {
    UartMode mode = UartMode::None;
    MidiLinkMode midilink = MidiLinkMode::FSynth;

    static constexpr std::size_t kCardBytes = sizeof(std::uint32_t);

    [[nodiscard]] static UartPersist from_card(
        std::span<const std::uint8_t, kCardBytes> bytes
    ) noexcept;
    friend constexpr bool operator==(const UartPersist&, const UartPersist&) = default;
};

struct UartPersistWord {

    [[nodiscard]] constexpr UartPersist pair() const noexcept {
        return UartPersist{
            static_cast<UartMode>(v & kByteMask),
            static_cast<MidiLinkMode>((v >> kMidiLinkShift) & kByteMask)
        };
    }

    [[nodiscard]] static constexpr UartPersistWord from(UartPersist p) noexcept {
        UartPersistWord w{};
        w.v = static_cast<std::uint32_t>(p.mode)
            | (static_cast<std::uint32_t>(p.midilink) << kMidiLinkShift);
        return w;
    }

    [[nodiscard]] static UartPersistWord from_card(
        std::span<const std::uint8_t, UartPersist::kCardBytes> bytes
    ) noexcept;

    [[nodiscard]] std::array<std::uint8_t, UartPersist::kCardBytes> to_card() const noexcept;
    friend constexpr bool operator==(const UartPersistWord&, const UartPersistWord&) = default;

private:
    static constexpr unsigned kMidiLinkShift = 8;
    static constexpr std::uint32_t kByteMask = 0xFFu;
    std::uint32_t v = 0;
};

[[nodiscard]] UartPersist clamp_uart_modes(UartPersist p) noexcept;

}
