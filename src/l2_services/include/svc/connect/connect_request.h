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
#include "os/connect/country_code.h"
#include "os/connect/passkey.h"
#include "os/connect/passphrase.h"
#include "os/connect/pin_code.h"
#include "os/connect/ssid.h"
#include "os/connect/wifi_security.h"
#include "svc/connect/bt_page_mode.h"
#include "svc/connect/prompt_answer.h"
#include "svc/connect/prompt_gen.h"

namespace mister::svc::connect {

struct ConnectRequest {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t {
        WifiPage,
        WifiScan,
        WifiJoin,
        WifiForget,
        WifiForgetAll,
        WifiSetCountry,
        BtPage,
        BtStopAutoFirst,
        BtPair,
        BtCancelPair,
        BtConnect,
        BtDisconnect,
        BtRemove,
        BtRemoveAll,
        BtAnswer,
        BtResetAdapter,
        kCount,
    };

    static constexpr std::size_t kStore = 100;
    static constexpr std::size_t kStoreAlign = 4;

    struct WifiPage {
        static constexpr Kind kKind = Kind::WifiPage;
        bool open = false;
    };

    struct WifiScan {
        static constexpr Kind kKind = Kind::WifiScan;
    };

    struct WifiJoin {
        static constexpr Kind kKind = Kind::WifiJoin;
        os::connect::WifiSecurity security = os::connect::WifiSecurity::Open;
        bool hidden = false;
        os::connect::Ssid ssid{};
        os::connect::Passphrase pass{};
    };

    struct WifiForget {
        static constexpr Kind kKind = Kind::WifiForget;
        os::connect::Ssid ssid{};
    };

    struct WifiForgetAll {
        static constexpr Kind kKind = Kind::WifiForgetAll;
    };

    struct WifiSetCountry {
        static constexpr Kind kKind = Kind::WifiSetCountry;
        os::connect::CountryCode code{};
    };

    struct BtPage {
        static constexpr Kind kKind = Kind::BtPage;
        BtPageMode mode = BtPageMode::Closed;
        bool auto_first = false;
    };

    struct BtStopAutoFirst {
        static constexpr Kind kKind = Kind::BtStopAutoFirst;
    };

    struct BtPair {
        static constexpr Kind kKind = Kind::BtPair;
        os::connect::BtAddr addr{};
    };

    struct BtCancelPair {
        static constexpr Kind kKind = Kind::BtCancelPair;
    };

    struct BtConnect {
        static constexpr Kind kKind = Kind::BtConnect;
        os::connect::BtAddr addr{};
    };

    struct BtDisconnect {
        static constexpr Kind kKind = Kind::BtDisconnect;
        os::connect::BtAddr addr{};
    };

    struct BtRemove {
        static constexpr Kind kKind = Kind::BtRemove;
        os::connect::BtAddr addr{};
    };

    struct BtRemoveAll {
        static constexpr Kind kKind = Kind::BtRemoveAll;
    };

    struct BtAnswer {
        static constexpr Kind kKind = Kind::BtAnswer;
        PromptGen gen{};
        PromptAnswer answer = PromptAnswer::Cancel;
        std::array<std::byte, 3> pad_{};
        os::connect::Passkey passkey = os::connect::Passkey::zero();
        os::connect::PinCode pin{};
        std::array<std::byte, 3> tail_pad_{};
    };

    struct BtResetAdapter {
        static constexpr Kind kKind = Kind::BtResetAdapter;
    };

    using Alternatives = std::tuple<
        WifiPage,
        WifiScan,
        WifiJoin,
        WifiForget,
        WifiForgetAll,
        WifiSetCountry,
        BtPage,
        BtStopAutoFirst,
        BtPair,
        BtCancelPair,
        BtConnect,
        BtDisconnect,
        BtRemove,
        BtRemoveAll,
        BtAnswer,
        BtResetAdapter>;

    Kind kind = Kind::WifiScan;
    std::array<std::byte, 3> pad_{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<ConnectRequest> && infra::alternatives_are_total<ConnectRequest>());
static_assert(sizeof(ConnectRequest::WifiJoin) == 100);
static_assert(sizeof(ConnectRequest::BtAnswer) == 32);
static_assert(sizeof(ConnectRequest) == 104);

}
