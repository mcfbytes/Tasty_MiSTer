// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "infra/error.h"
#include "infra/fixed_ring.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/country_code.h"
#include "os/if_name.h"
#include "os/passphrase.h"
#include "os/raw_monotonic_clock.h"
#include "os/ssid.h"
#include "os/unix_dgram_socket.h"
#include "os/wifi_security.h"
#include "os/wpa_event.h"
#include "os/wpa_network_id.h"

namespace mister::os {

struct WpaCtrlCommand {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t {
        Ping,
        Status,
        Scan,
        ScanResults,
        ListNetworks,
        AddNetwork,
        SetSsid,
        SetPsk,
        SetKeyMgmt,
        SetIeee80211w,
        SetHidden,
        Select,
        Enable,
        Remove,
        Disconnect,
        Reconnect,
        SetCountry,
        SignalPoll,
        kCount,
    };

    enum : std::size_t { kStore = 72, kStoreAlign = 4 };

    struct Padding {
        std::array<std::uint8_t, 3> bytes{};
    };

    static_assert(sizeof(Padding) == 3);

    struct Ping {
        static constexpr Kind kKind = Kind::Ping;
    };

    struct Status {
        static constexpr Kind kKind = Kind::Status;
    };

    struct Scan {
        static constexpr Kind kKind = Kind::Scan;
    };

    struct ScanResults {
        static constexpr Kind kKind = Kind::ScanResults;
    };

    struct ListNetworks {
        static constexpr Kind kKind = Kind::ListNetworks;
    };

    struct AddNetwork {
        static constexpr Kind kKind = Kind::AddNetwork;
    };

    struct SetSsid {
        static constexpr Kind kKind = Kind::SetSsid;
        Ssid ssid{};
        Padding pad_{};
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct SetPsk {
        static constexpr Kind kKind = Kind::SetPsk;
        Passphrase psk{};
        Padding pad_{};
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct SetKeyMgmt {
        static constexpr Kind kKind = Kind::SetKeyMgmt;
        WifiSecurity security = WifiSecurity::Open;
        Padding pad_{};
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct SetIeee80211w {
        static constexpr Kind kKind = Kind::SetIeee80211w;
        WifiSecurity security = WifiSecurity::Open;
        Padding pad_{};
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct SetHidden {
        static constexpr Kind kKind = Kind::SetHidden;
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct Select {
        static constexpr Kind kKind = Kind::Select;
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct Enable {
        static constexpr Kind kKind = Kind::Enable;
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct Remove {
        static constexpr Kind kKind = Kind::Remove;
        WpaNetworkId id = *WpaNetworkId::of(0);
    };

    struct Disconnect {
        static constexpr Kind kKind = Kind::Disconnect;
    };

    struct Reconnect {
        static constexpr Kind kKind = Kind::Reconnect;
    };

    struct SetCountry {
        static constexpr Kind kKind = Kind::SetCountry;
        CountryCode code{};
    };

    struct SignalPoll {
        static constexpr Kind kKind = Kind::SignalPoll;
    };

    using Alternatives = std::tuple<
        Ping,
        Status,
        Scan,
        ScanResults,
        ListNetworks,
        AddNetwork,
        SetSsid,
        SetPsk,
        SetKeyMgmt,
        SetIeee80211w,
        SetHidden,
        Select,
        Enable,
        Remove,
        Disconnect,
        Reconnect,
        SetCountry,
        SignalPoll>;

    Kind kind = Kind::Ping;
    Padding pad_{};
    alignas(kStoreAlign) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<WpaCtrlCommand> && infra::alternatives_are_total<WpaCtrlCommand>());
static_assert(sizeof(WpaCtrlCommand) == 76 && alignof(WpaCtrlCommand) == 4);

struct WpaCtrlReply {
    TASTY_SEAT_EXEMPT(component);

    WpaCtrlCommand::Kind kind = WpaCtrlCommand::Kind::Ping;
    std::string_view text{};
};

static_assert(std::is_trivially_copyable_v<WpaCtrlReply>);

struct WpaCtrlPollResult {
    TASTY_SEAT_EXEMPT(component);

    enum Status : std::uint8_t { Idle, Reply, TimedOut };

    enum : std::size_t { kMaxEvents = 8 };

    Status status = Idle;
    std::optional<WpaCtrlReply> reply{};

    std::array<std::optional<WpaEvent>, kMaxEvents> events{};

    [[nodiscard]] auto drained() const& noexcept {
        return events
            | std::views::take_while([](const std::optional<WpaEvent>& slot) {
                   return slot.has_value();
               })
            | std::views::transform([](const std::optional<WpaEvent>& slot) { return *slot; });
    }

    auto drained() const&& = delete;
};

class WpaCtrl {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::string_view kControlDir = "/run/wpa_supplicant";
    static constexpr std::chrono::seconds kReplyDeadline{5};

    enum : std::size_t { kQueueDepth = 8, kReceiveBuffer = 4096 };

    [[nodiscard]] static Ex<std::optional<WpaCtrl>> open(IfName ifname) noexcept;
    [[nodiscard]] static Ex<std::optional<WpaCtrl>> open_in(
        std::string_view control_dir,
        IfName ifname
    ) noexcept;

    WpaCtrl(WpaCtrl&&) noexcept = default;
    WpaCtrl& operator=(WpaCtrl&&) noexcept = delete;
    WpaCtrl(const WpaCtrl&) = delete;
    WpaCtrl& operator=(const WpaCtrl&) = delete;
    ~WpaCtrl();

    [[nodiscard]] Ex<void> submit(WpaCtrlCommand command) noexcept;
    [[nodiscard]] WpaCtrlPollResult poll(RawMonotonicClock::time_point now) noexcept;

    [[nodiscard]] const UniqueFd* cmd_fd() const noexcept;
    [[nodiscard]] const UniqueFd* mon_fd() const noexcept;

    void close() noexcept;

private:
    WpaCtrl(UnixDgramSocket command, UnixDgramSocket monitor) noexcept;

    void reset_head() noexcept;
    void pop_head() noexcept;
    [[nodiscard]] bool send_head_once() noexcept;
    void scrub_head_secret() noexcept;
    void drain_monitor(WpaCtrlPollResult& out) noexcept;
    [[nodiscard]] bool drain_command(
        RawMonotonicClock::time_point now,
        WpaCtrlPollResult& out
    ) noexcept;

    std::optional<UnixDgramSocket> cmd_{};
    std::optional<UnixDgramSocket> monitor_{};

    infra::FixedRing<WpaCtrlCommand, kQueueDepth> queue_{};
    bool sent_ = false;
    bool failed_ = false;
    std::optional<RawMonotonicClock::time_point> sent_at_{};
    std::array<std::byte, kReceiveBuffer> rx_{};
};

class WpaCtrlOpener {
    TASTY_SEAT_RESIDENT(Connect);

public:
    using Link = WpaCtrl;

    [[nodiscard]] static Ex<std::optional<WpaCtrl>> open(IfName ifname) noexcept {
        return WpaCtrl::open(ifname);
    }
};

}
