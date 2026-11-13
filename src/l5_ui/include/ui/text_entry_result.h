// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/passkey.h"
#include "os/passphrase.h"
#include "os/pin_code.h"
#include "os/ssid.h"

namespace mister::ui {

struct TextEntryResult {
    TASTY_SEAT_RESIDENT(Ui);

    enum class Kind : std::uint8_t {
        Passphrase,
        Ssid,
        Passkey,
        Pin,
        kCount,
    };

    struct Passphrase {
        static constexpr Kind kKind = Kind::Passphrase;
        os::Passphrase value{};
    };

    struct Ssid {
        static constexpr Kind kKind = Kind::Ssid;
        os::Ssid value{};
    };

    struct Passkey {
        static constexpr Kind kKind = Kind::Passkey;
        os::Passkey value = os::Passkey::zero();
    };

    struct Pin {
        static constexpr Kind kKind = Kind::Pin;
        os::PinCode value{};
    };

    using Alternatives = std::tuple<Passphrase, Ssid, Passkey, Pin>;

    static constexpr std::size_t kStoreAlign = std::max(
        {alignof(Passphrase), alignof(Ssid), alignof(Passkey), alignof(Pin)}
    );
    static constexpr std::size_t
        kStore = (std::max({sizeof(Passphrase), sizeof(Ssid), sizeof(Passkey), sizeof(Pin)})
                  + kStoreAlign
                  - 1)
        / kStoreAlign
        * kStoreAlign;

    Kind kind = Kind::Passphrase;
    std::array<std::byte, 3> pad_{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};
};

static_assert(
    infra::MessageSum<TextEntryResult> && infra::alternatives_are_total<TextEntryResult>()
);

}
