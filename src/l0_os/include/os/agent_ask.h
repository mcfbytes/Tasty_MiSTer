// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/bt_device_path.h"
#include "os/dbus_bus_name.h"
#include "os/dbus_serial.h"
#include "os/passkey.h"
#include "os/passkey_digits.h"
#include "os/pin_code.h"

namespace mister::os {

inline constexpr std::size_t kAgentServiceChars = 36;

struct AgentAsk {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t {
        Release,
        RequestPinCode,
        DisplayPinCode,
        RequestPasskey,
        DisplayPasskey,
        RequestConfirmation,
        RequestAuthorization,
        AuthorizeService,
        Cancel,
        kCount,
    };

    struct Head {
        DbusSerial serial{};
        DbusBusName sender{};
        std::uint16_t pad_ = 0;
    };

    static constexpr std::size_t kStore = 52;
    static constexpr std::size_t kStoreAlign = 4;

    struct Release {
        static constexpr Kind kKind = Kind::Release;
    };

    struct RequestPinCode {
        static constexpr Kind kKind = Kind::RequestPinCode;
        BtDevicePath device{};
    };

    struct DisplayPinCode {
        static constexpr Kind kKind = Kind::DisplayPinCode;
        BtDevicePath device{};
        PinCode pin{};
    };

    struct RequestPasskey {
        static constexpr Kind kKind = Kind::RequestPasskey;
        BtDevicePath device{};
    };

    struct DisplayPasskey {
        static constexpr Kind kKind = Kind::DisplayPasskey;
        Passkey passkey = *Passkey::of(0);
        PasskeyDigits digits = *PasskeyDigits::of(0);
        BtDevicePath device{};
    };

    struct RequestConfirmation {
        static constexpr Kind kKind = Kind::RequestConfirmation;
        Passkey passkey = *Passkey::of(0);
        BtDevicePath device{};
        std::uint8_t pad_[1]{};
    };

    struct RequestAuthorization {
        static constexpr Kind kKind = Kind::RequestAuthorization;
        BtDevicePath device{};
    };

    struct AuthorizeService {
        static constexpr Kind kKind = Kind::AuthorizeService;
        FixedStr<kAgentServiceChars + 2, StrFit::Clip> service{};
        BtDevicePath device{};
        std::uint8_t pad_[1]{};
    };

    struct Cancel {
        static constexpr Kind kKind = Kind::Cancel;
    };

    using Alternatives = std::tuple<
        Release,
        RequestPinCode,
        DisplayPinCode,
        RequestPasskey,
        DisplayPasskey,
        RequestConfirmation,
        RequestAuthorization,
        AuthorizeService,
        Cancel>;

    Kind kind = Kind::Release;
    std::uint8_t pad_[3]{};
    Head head{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<AgentAsk> && infra::alternatives_are_total<AgentAsk>());
static_assert(sizeof(AgentAsk::Head) == 264 && alignof(AgentAsk::Head) == 4);
static_assert(sizeof(AgentAsk::DisplayPasskey) == 12);
static_assert(sizeof(AgentAsk::RequestConfirmation) == 12);
static_assert(sizeof(AgentAsk::AuthorizeService) == 50);
static_assert(sizeof(AgentAsk) == 320);
static_assert(std::has_unique_object_representations_v<AgentAsk>);

}
