// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "cores/ide/atapi_sense.h"
#include "infra/byte_count.h"
#include "infra/message_sum.h"

namespace mister::cores::ide {

struct AtapiStep {

    struct DataIn {
        infra::ByteCount bytes{};
        friend constexpr bool operator==(DataIn, DataIn) = default;
    };

    struct DataOut {
        infra::ByteCount bytes{};
        friend constexpr bool operator==(DataOut, DataOut) = default;
    };

    struct Status {
        AtapiSenseKey key = AtapiSenseKey::NoSense;
        bool seek_complete = false;

        [[nodiscard]] constexpr bool check() const noexcept {
            return key != AtapiSenseKey::NoSense;
        }

        [[nodiscard]] constexpr std::byte error_register() const noexcept {
            return error_register_of(key);
        }

        friend constexpr bool operator==(Status, Status) = default;
    };

    enum class Kind : std::uint8_t { DataIn, DataOut, Status, kCount };
    using Phase = std::variant<DataIn, DataOut, Status>;
    static_assert(
        std::variant_size_v<Phase> == infra::ordinal(Kind::kCount),
        "AtapiStep: one Kind name per arm"
    );
    template <Kind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Phase>, T>;
    static_assert(
        kArmIs<Kind::DataIn, DataIn>
        && kArmIs<Kind::DataOut, DataOut>
        && kArmIs<Kind::Status, Status>
    );

    Phase phase{Status{}};

    [[nodiscard]] constexpr Kind kind() const noexcept { return static_cast<Kind>(phase.index()); }

    friend constexpr bool operator==(const AtapiStep&, const AtapiStep&) = default;
};

static_assert(
    AtapiStep::Status{.key = AtapiSenseKey::UnitAttention}.error_register() == std::byte{0x60},
    "unit attention's nibble already holds the ATA media-changed bit"
);
static_assert(
    AtapiStep::Status{.key = AtapiSenseKey::NotReady}.error_register() == std::byte{0x20}
);

}
