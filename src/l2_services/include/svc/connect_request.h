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
#include "os/country_code.h"
#include "os/passkey.h"
#include "os/passphrase.h"
#include "os/pin_code.h"
#include "os/ssid.h"
#include "os/wifi_security.h"
#include "svc/bt_page_mode.h"
#include "svc/prompt_answer.h"
#include "svc/prompt_gen.h"

namespace mister::svc {

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
        os::WifiSecurity security = os::WifiSecurity::Open;
        bool hidden = false;
        os::Ssid ssid{};
        os::Passphrase pass{};
    };

    struct WifiForget {
        static constexpr Kind kKind = Kind::WifiForget;
        os::Ssid ssid{};
    };

    struct WifiForgetAll {
        static constexpr Kind kKind = Kind::WifiForgetAll;
    };

    struct WifiSetCountry {
        static constexpr Kind kKind = Kind::WifiSetCountry;
        os::CountryCode code{};
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
        os::BtAddr addr{};
    };

    struct BtCancelPair {
        static constexpr Kind kKind = Kind::BtCancelPair;
    };

    struct BtConnect {
        static constexpr Kind kKind = Kind::BtConnect;
        os::BtAddr addr{};
    };

    struct BtDisconnect {
        static constexpr Kind kKind = Kind::BtDisconnect;
        os::BtAddr addr{};
    };

    struct BtRemove {
        static constexpr Kind kKind = Kind::BtRemove;
        os::BtAddr addr{};
    };

    struct BtRemoveAll {
        static constexpr Kind kKind = Kind::BtRemoveAll;
    };

    struct BtAnswer {
        static constexpr Kind kKind = Kind::BtAnswer;
        PromptGen gen{};
        PromptAnswer answer = PromptAnswer::Cancel;
        std::array<std::byte, 3> pad_{};
        os::Passkey passkey = os::Passkey::zero();
        os::PinCode pin{};
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
