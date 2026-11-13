// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/uart_persist.h"

#include "app/midi_link_facts.h"
#include "app/uart_mode_facts.h"
#include "infra/endian.h"

namespace mister::app {

UartPersist UartPersist::from_card(std::span<const std::uint8_t, kCardBytes> bytes) noexcept {
    return UartPersistWord::from_card(bytes).pair();
}

UartPersistWord UartPersistWord::from_card(
    std::span<const std::uint8_t, UartPersist::kCardBytes> bytes
) noexcept {
    UartPersistWord w{};
    w.v = infra::load_le<std::uint32_t>(bytes);
    return w;
}

std::array<std::uint8_t, UartPersist::kCardBytes> UartPersistWord::to_card() const noexcept {
    std::array<std::uint8_t, UartPersist::kCardBytes> out{};
    const std::span<std::uint8_t, UartPersist::kCardBytes> card{out};
    infra::store_le<std::uint32_t>(card, v);
    return out;
}

UartPersist clamp_uart_modes(UartPersist p) noexcept {
    const UartModeFacts row = uart_mode_facts(p.mode);
    if (!row.admits || midi_link_facts(p.midilink).group != *row.admits)
        p.midilink = row.fallback;
    return p;
}

}
