// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/uart_binding.h"

#include <cstddef>

#include "infra/endian.h"

namespace mister::app {
namespace {

constexpr cores::BaudRate kMidiBaud{31250};

}

UartBinding::SpeedsFile UartBinding::SpeedsFile::from_card(
    std::span<const std::uint8_t, kBytes> bytes
) noexcept {
    constexpr std::size_t kWord = sizeof(std::uint32_t);
    constexpr std::size_t kThird = 2 * kWord;
    return SpeedsFile{
        cores::BaudRate{infra::load_le<std::uint32_t>(bytes.first<kWord>())},
        cores::BaudRate{infra::load_le<std::uint32_t>(bytes.subspan(kWord).first<kWord>())},
        cores::BaudRate{infra::load_le<std::uint32_t>(bytes.subspan(kThird).first<kWord>())}
    };
}

UartBinding::Picks UartBinding::SpeedsFile::resolved(cores::BaudRate uart_first) const noexcept {
    return Picks{uart, midi, mlink != cores::BaudRate{} ? mlink : uart_first};
}

UartBinding::Files UartBinding::Files::from_card(
    const std::optional<std::array<std::uint8_t, UartPersist::kCardBytes>>& mode,
    const std::optional<std::array<std::uint8_t, SpeedsFile::kBytes>>& speeds
) noexcept {
    Files out{};
    if (mode)
        out.mode = UartPersist::from_card(*mode);
    if (speeds)
        out.speeds = SpeedsFile::from_card(*speeds);
    return out;
}

UartBinding UartBinding::decode(const Inputs& in) noexcept {
    UartBinding b{};
    b.capable_ = in.capable;
    b.usb_ser_ = in.usb_ser;
    b.uart_ = BaudTable::parse(in.tokens.uart);
    b.midi_ = BaudTable::parse(in.tokens.midi);

    if (!in.tokens.midi.empty() && b.midi_.empty())
        b.midi_ = b.midi_.seeded(kMidiBaud);
    if (in.capable) {
        if (b.uart_.empty()) {
            b.uart_ = b.uart_.seeded(in.default_baud);

            if (b.midi_.empty())
                b.midi_ = b.midi_.seeded(kMidiBaud);
        }

        b.persist_ = clamp_uart_modes(in.files.mode.value_or(UartPersist{}));

        b.picks_ = in.files.speeds.value_or(SpeedsFile{}).resolved(b.uart_.first());
    }
    return b;
}

std::optional<UartBinding> UartBinding::of(std::span<const std::uint8_t> bytes) noexcept {

    if (bytes.size() != sizeof(UartBinding))
        return std::nullopt;
    if (bytes[offsetof(UartBinding, capable_)] > 1 || bytes[offsetof(UartBinding, usb_ser_)] > 1)
        return std::nullopt;
    return infra::load_native<UartBinding>(bytes.first<sizeof(UartBinding)>());
}

std::optional<UartBinding> UartBinding::from_image(std::span<const std::uint8_t> bytes) noexcept {
    return of(bytes);
}

}
