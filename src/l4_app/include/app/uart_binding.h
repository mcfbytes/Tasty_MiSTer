// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "app/baud_table.h"
#include "app/link_tx_reader.h"
#include "app/uart_persist.h"
#include "cores/types.h"
#include "infra/byte_view.h"

namespace mister::app {

class UartBinding {
public:
    struct Tokens {
        std::string_view uart;
        std::string_view midi;
    };

    struct Picks {
        cores::BaudRate uart{};
        cores::BaudRate midi{};
        cores::BaudRate mlink{};
        friend constexpr bool operator==(const Picks&, const Picks&) = default;
    };

    struct SpeedsFile {
        static constexpr std::size_t kSlots = 3;
        static constexpr std::size_t kBytes = kSlots * sizeof(std::uint32_t);
        cores::BaudRate uart{};
        cores::BaudRate midi{};
        cores::BaudRate mlink{};

        [[nodiscard]] static SpeedsFile from_card(
            std::span<const std::uint8_t, kBytes> bytes
        ) noexcept;

        [[nodiscard]] Picks resolved(cores::BaudRate uart_first) const noexcept;
        friend constexpr bool operator==(const SpeedsFile&, const SpeedsFile&) = default;
    };

    struct Files {
        std::optional<UartPersist> mode{};
        std::optional<SpeedsFile> speeds{};
        [[nodiscard]] static Files from_card(
            const std::optional<std::array<std::uint8_t, UartPersist::kCardBytes>>& mode,
            const std::optional<std::array<std::uint8_t, SpeedsFile::kBytes>>& speeds
        ) noexcept;
    };

    struct Inputs {
        bool capable = false;
        bool usb_ser = false;
        Tokens tokens{};
        Files files{};
        cores::BaudRate default_baud{cores::kDefaultUartBaud};
    };

    [[nodiscard]] static UartBinding decode(const Inputs& in) noexcept;

    [[nodiscard]] static std::optional<UartBinding> from_image(
        std::span<const std::uint8_t> bytes
    ) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> image() const noexcept {
        return infra::byte_cast<const std::uint8_t>(infra::bytes_of(*this));
    }

    [[nodiscard]] bool capable() const noexcept { return capable_; }

    [[nodiscard]] bool usb_ser() const noexcept { return usb_ser_; }

    [[nodiscard]] const UartPersist& persist() const noexcept { return persist_; }

    [[nodiscard]] const BaudTable& uart() const noexcept { return uart_; }

    [[nodiscard]] const BaudTable& midi() const noexcept { return midi_; }

    [[nodiscard]] const Picks& picks() const noexcept { return picks_; }

    [[nodiscard]] bool is_inert() const noexcept {
        return !capable_ && uart_.empty() && midi_.empty();
    }

private:
    [[nodiscard]] static std::optional<UartBinding> of(
        std::span<const std::uint8_t> bytes
    ) noexcept;

    UartPersist persist_{};
    bool capable_ = false;
    bool usb_ser_ = false;
    BaudTable uart_{};
    BaudTable midi_{};
    Picks picks_{};
};

static_assert(std::is_trivially_copyable_v<UartBinding> && std::is_standard_layout_v<UartBinding>);
static_assert(
    sizeof(UartBinding)
    == sizeof(UartPersist) + 2 * sizeof(bool) + 2 * sizeof(BaudTable) + sizeof(UartBinding::Picks)
);
static_assert(sizeof(UartBinding) <= kLinkTxSlabBytes, "the binding is one Tx slab slot");

}
