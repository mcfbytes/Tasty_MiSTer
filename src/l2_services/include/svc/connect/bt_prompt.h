// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/connect/bt_addr.h"
#include "os/connect/bt_kind.h"
#include "os/connect/passkey.h"
#include "os/connect/passkey_digits.h"
#include "os/connect/pin_code.h"
#include "svc/connect/prompt_gen.h"

namespace mister::svc::connect {

struct BtPrompt {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t {
        ShowPasskey,
        ShowPin,
        Confirm,
        EnterPasskey,
        EnterPin,
        Authorize,
        kCount,
    };

    static constexpr std::size_t kStore = 28;
    static constexpr std::size_t kStoreAlign = 4;

    struct ShowPasskey {
        static constexpr Kind kKind = Kind::ShowPasskey;
        PromptGen gen{};
        os::connect::Passkey passkey = *os::connect::Passkey::of(0);
        os::connect::BtAddr addr{};
        os::connect::PasskeyDigits digits = *os::connect::PasskeyDigits::of(0);
        std::uint8_t pad_[1]{};
    };

    struct ShowPin {
        static constexpr Kind kKind = Kind::ShowPin;
        PromptGen gen{};
        os::connect::BtAddr addr{};
        os::connect::PinCode pin{};
        std::uint8_t pad_[1]{};
    };

    struct Confirm {
        static constexpr Kind kKind = Kind::Confirm;
        PromptGen gen{};
        os::connect::Passkey passkey = *os::connect::Passkey::of(0);
        os::connect::BtAddr addr{};
        std::uint8_t pad_[2]{};
    };

    struct EnterPasskey {
        static constexpr Kind kKind = Kind::EnterPasskey;
        PromptGen gen{};
        os::connect::BtAddr addr{};
        std::uint8_t pad_[2]{};
    };

    struct EnterPin {
        static constexpr Kind kKind = Kind::EnterPin;
        PromptGen gen{};
        os::connect::BtAddr addr{};
        std::uint8_t pad_[2]{};
    };

    struct Authorize {
        static constexpr Kind kKind = Kind::Authorize;
        PromptGen gen{};
        os::connect::BtAddr addr{};
        os::connect::BtKind kind = os::connect::BtKind::Other;
        std::uint8_t pad_[1]{};
    };

    using Alternatives = std::
        tuple<ShowPasskey, ShowPin, Confirm, EnterPasskey, EnterPin, Authorize>;

    Kind kind = Kind::ShowPin;
    std::uint8_t pad_[3]{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};

    friend bool operator==(const BtPrompt& a, const BtPrompt& b) noexcept {
        return a.kind == b.kind && a.store == b.store;
    }

    friend bool operator!=(const BtPrompt& a, const BtPrompt& b) noexcept { return !(a == b); }
};

static_assert(infra::MessageSum<BtPrompt> && infra::alternatives_are_total<BtPrompt>());
static_assert(sizeof(BtPrompt::ShowPasskey) == 16);
static_assert(sizeof(BtPrompt::ShowPin) == 28);
static_assert(sizeof(BtPrompt::Confirm) == 16);
static_assert(sizeof(BtPrompt::EnterPasskey) == 12);
static_assert(sizeof(BtPrompt::EnterPin) == 12);
static_assert(sizeof(BtPrompt::Authorize) == 12);
static_assert(sizeof(BtPrompt) == 32);

}
