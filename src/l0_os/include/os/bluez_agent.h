// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/agent_ask.h"
#include "os/dbus_connection.h"
#include "os/dbus_header.h"
#include "os/dbus_incoming.h"
#include "os/passkey.h"

namespace mister::os {

class BluezAgent {
    TASTY_SEAT_RESIDENT(Connect);

public:
    static constexpr std::string_view kPath{"/org/mister/agent"};
    static constexpr std::string_view kInterface{"org.bluez.Agent1"};
    static constexpr std::string_view kRejected{"org.bluez.Error.Rejected"};
    static constexpr std::string_view kCanceled{"org.bluez.Error.Canceled"};
    static constexpr std::string_view kUnknownMethod{"org.freedesktop.DBus.Error.UnknownMethod"};
    static constexpr std::string_view kUnknownObject{"org.freedesktop.DBus.Error.UnknownObject"};

    [[nodiscard]] static Ex<std::optional<AgentAsk>> decode(const DbusIncoming& call) noexcept;

    [[nodiscard]] static Ex<DbusHeader> accepted_reply(const DbusIncoming& call) noexcept;
    [[nodiscard]] static Ex<DbusHeader> pin_reply(const DbusIncoming& call) noexcept;
    [[nodiscard]] static Ex<DbusHeader> passkey_reply(const DbusIncoming& call) noexcept;
    [[nodiscard]] static Ex<DbusHeader> rejected_reply(const DbusIncoming& call) noexcept;
    [[nodiscard]] static Ex<DbusHeader> canceled_reply(const DbusIncoming& call) noexcept;
    [[nodiscard]] static Ex<DbusHeader> unknown_reply(const DbusIncoming& call) noexcept;

    [[nodiscard]] static Ex<std::span<const std::byte>> encode_pin(
        std::string_view pin,
        std::span<std::byte> arena
    ) noexcept;
    [[nodiscard]] static Ex<std::span<const std::byte>> encode_passkey(
        Passkey passkey,
        std::span<std::byte> arena
    ) noexcept;

    [[nodiscard]] static Ex<void> send(
        DbusConnection& bus,
        const DbusIncoming& call,
        const DbusHeader& reply,
        std::span<const std::byte> body
    ) noexcept;

    [[nodiscard]] static Ex<std::optional<AgentAsk>> handle(
        DbusConnection& bus,
        const DbusIncoming& call
    ) noexcept;

private:
    [[nodiscard]] static Ex<DbusHeader> reply_header(
        const DbusIncoming& call,
        DbusHeader::Type type,
        std::string_view error_name,
        std::string_view signature
    ) noexcept;
};

}
