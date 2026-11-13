// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/bt_addr.h"
#include "os/bt_kind.h"
#include "os/passkey.h"
#include "os/passkey_digits.h"
#include "os/pin_code.h"
#include "svc/prompt_gen.h"

namespace mister::svc {

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
        os::Passkey passkey = *os::Passkey::of(0);
        os::BtAddr addr{};
        os::PasskeyDigits digits = *os::PasskeyDigits::of(0);
        std::uint8_t pad_[1]{};
    };

    struct ShowPin {
        static constexpr Kind kKind = Kind::ShowPin;
        PromptGen gen{};
        os::BtAddr addr{};
        os::PinCode pin{};
        std::uint8_t pad_[1]{};
    };

    struct Confirm {
        static constexpr Kind kKind = Kind::Confirm;
        PromptGen gen{};
        os::Passkey passkey = *os::Passkey::of(0);
        os::BtAddr addr{};
        std::uint8_t pad_[2]{};
    };

    struct EnterPasskey {
        static constexpr Kind kKind = Kind::EnterPasskey;
        PromptGen gen{};
        os::BtAddr addr{};
        std::uint8_t pad_[2]{};
    };

    struct EnterPin {
        static constexpr Kind kKind = Kind::EnterPin;
        PromptGen gen{};
        os::BtAddr addr{};
        std::uint8_t pad_[2]{};
    };

    struct Authorize {
        static constexpr Kind kKind = Kind::Authorize;
        PromptGen gen{};
        os::BtAddr addr{};
        os::BtKind kind = os::BtKind::Other;
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
