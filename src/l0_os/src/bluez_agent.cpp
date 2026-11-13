// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/bluez_agent.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/decimal_text.h"
#include "infra/error.h"
#include "os/dbus_header.h"
#include "os/dbus_message.h"
#include "os/dbus_reader.h"
#include "os/dbus_writer.h"

namespace mister::os {
namespace {

constexpr WireBytes kUuidTextMax{kAgentServiceChars};

[[nodiscard]] Error format_error() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] Ex<BtDevicePath> device_from(const DbusObjectPath& path) noexcept {
    auto device = BtDevicePath::parse(path.view());
    if (!device)
        return std::unexpected(device.error());
    return *device;
}

[[nodiscard]] Ex<Passkey> read_passkey(DbusReader& reader) noexcept {
    auto value = reader.u32();
    if (!value)
        return std::unexpected(value.error());
    return Passkey::parse(value->decimal().view());
}

[[nodiscard]] Ex<PasskeyDigits> read_digits(DbusReader& reader) noexcept {
    auto value = reader.u16();
    if (!value)
        return std::unexpected(value.error());
    return PasskeyDigits::parse(value->decimal().view());
}

}

Ex<std::optional<AgentAsk>> BluezAgent::decode(const DbusIncoming& call) noexcept {
    if (call.header.type != DbusHeader::Type::MethodCall)
        return std::optional<AgentAsk>{};
    if (call.header.path() != kPath)
        return std::optional<AgentAsk>{};
    if (call.header.interface_name() != kInterface)
        return std::optional<AgentAsk>{};

    const std::string_view member = call.header.member();
    const std::string_view signature = call.header.signature();
    const DbusSerial serial = call.header.serial;
    auto sender = DbusBusName::parse(call.header.sender());
    if (!sender)
        return std::unexpected(sender.error());
    const AgentAsk::Head head{serial, *sender, 0};

    if (member == "Release" || member == "Cancel") {
        if (!signature.empty())
            return std::unexpected(format_error());
        if (member == "Release") {
            return std::optional<AgentAsk>{infra::make<AgentAsk>(AgentAsk::Release{}, head)};
        }
        return std::optional<AgentAsk>{infra::make<AgentAsk>(AgentAsk::Cancel{}, head)};
    }

    DbusReader reader{call.body, false};

    if (member == "RequestPinCode") {
        if (signature != "o")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        return std::optional<AgentAsk>{
            infra::make<AgentAsk>(AgentAsk::RequestPinCode{*device}, head)
        };
    }
    if (member == "DisplayPinCode") {
        if (signature != "os")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        auto pin = reader.string();
        if (!pin)
            return std::unexpected(pin.error());
        auto code = PinCode::parse(*pin);
        if (!code)
            return std::unexpected(code.error());
        if (!reader.at_end())
            return std::unexpected(format_error());
        return std::optional<AgentAsk>{
            infra::make<AgentAsk>(AgentAsk::DisplayPinCode{*device, *code}, head)
        };
    }
    if (member == "RequestPasskey") {
        if (signature != "o")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        return std::optional<AgentAsk>{
            infra::make<AgentAsk>(AgentAsk::RequestPasskey{*device}, head)
        };
    }
    if (member == "DisplayPasskey") {
        if (signature != "ouq")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        auto passkey = read_passkey(reader);
        if (!passkey)
            return std::unexpected(passkey.error());
        auto digits = read_digits(reader);
        if (!digits)
            return std::unexpected(digits.error());
        if (!reader.at_end())
            return std::unexpected(format_error());
        return std::optional<AgentAsk>{
            infra::make<AgentAsk>(AgentAsk::DisplayPasskey{*passkey, *digits, *device}, head)
        };
    }
    if (member == "RequestConfirmation") {
        if (signature != "ou")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        auto passkey = read_passkey(reader);
        if (!passkey)
            return std::unexpected(passkey.error());
        if (!reader.at_end())
            return std::unexpected(format_error());
        return std::optional<AgentAsk>{
            infra::make<AgentAsk>(AgentAsk::RequestConfirmation{*passkey, *device, 0}, head)
        };
    }
    if (member == "RequestAuthorization") {
        if (signature != "o")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        return std::optional<AgentAsk>{
            infra::make<AgentAsk>(AgentAsk::RequestAuthorization{*device}, head)
        };
    }
    if (member == "AuthorizeService") {
        if (signature != "os")
            return std::unexpected(format_error());
        auto path = reader.object_path();
        if (!path)
            return std::unexpected(path.error());
        auto device = device_from(*path);
        if (!device)
            return std::unexpected(device.error());
        auto service = reader.string();
        if (!service)
            return std::unexpected(service.error());
        AgentAsk::AuthorizeService alternative{};
        if (WireBytes{service->size()} > kUuidTextMax)
            return std::unexpected(format_error());
        if (!alternative.service.assign(*service))
            return std::unexpected(format_error());
        alternative.device = *device;
        if (!reader.at_end())
            return std::unexpected(format_error());
        return std::optional<AgentAsk>{infra::make<AgentAsk>(alternative, head)};
    }
    return std::optional<AgentAsk>{};
}

Ex<DbusHeader> BluezAgent::reply_header(
    const DbusIncoming& call,
    DbusHeader::Type type,
    std::string_view error_name,
    std::string_view signature
) noexcept {
    DbusHeader header{};
    header.type = type;
    if (!error_name.empty()) {
        auto added = header.add_field(DbusHeaderField::Code::ErrorName, error_name);
        if (!added)
            return std::unexpected(added.error());
    }
    if (!signature.empty()) {
        auto added = header.add_field(DbusHeaderField::Code::Signature, signature);
        if (!added)
            return std::unexpected(added.error());
    }
    const std::string_view sender = call.header.sender();
    if (!sender.empty()) {
        auto added = header.add_field(DbusHeaderField::Code::Destination, sender);
        if (!added)
            return std::unexpected(added.error());
    }
    auto reply_to = header.add_reply_serial(call.header.serial);
    if (!reply_to)
        return std::unexpected(reply_to.error());
    return header;
}

Ex<DbusHeader> BluezAgent::accepted_reply(const DbusIncoming& call) noexcept {
    return reply_header(call, DbusHeader::Type::MethodReturn, {}, {});
}

Ex<DbusHeader> BluezAgent::pin_reply(const DbusIncoming& call) noexcept {
    return reply_header(call, DbusHeader::Type::MethodReturn, {}, "s");
}

Ex<DbusHeader> BluezAgent::passkey_reply(const DbusIncoming& call) noexcept {
    return reply_header(call, DbusHeader::Type::MethodReturn, {}, "u");
}

Ex<DbusHeader> BluezAgent::rejected_reply(const DbusIncoming& call) noexcept {
    return reply_header(call, DbusHeader::Type::Error, kRejected, {});
}

Ex<DbusHeader> BluezAgent::canceled_reply(const DbusIncoming& call) noexcept {
    return reply_header(call, DbusHeader::Type::Error, kCanceled, {});
}

Ex<DbusHeader> BluezAgent::unknown_reply(const DbusIncoming& call) noexcept {
    const bool wrong_object = call.header.path() != kPath;
    return reply_header(
        call,
        DbusHeader::Type::Error,
        wrong_object ? kUnknownObject : kUnknownMethod,
        {}
    );
}

Ex<std::span<const std::byte>> BluezAgent::encode_pin(
    std::string_view pin,
    std::span<std::byte> arena
) noexcept {
    DbusWriter writer{arena};
    writer.string(pin);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return writer.written();
}

Ex<std::span<const std::byte>> BluezAgent::encode_passkey(
    Passkey passkey,
    std::span<std::byte> arena
) noexcept {
    DbusWriter writer{arena};
    writer.u32(passkey.wire());
    if (!writer.ok())
        return std::unexpected(writer.error());
    return writer.written();
}

Ex<void> BluezAgent::send(
    DbusConnection& bus,
    const DbusIncoming& call,
    const DbusHeader& reply,
    std::span<const std::byte> body
) noexcept {
    const DbusMessage message{reply, body};
    return bus.reply(call, message);
}

Ex<std::optional<AgentAsk>> BluezAgent::handle(
    DbusConnection& bus,
    const DbusIncoming& call
) noexcept {
    auto ask = decode(call);
    if (!ask)
        return std::unexpected(ask.error());
    if (ask->has_value())
        return ask;
    auto error = unknown_reply(call);
    if (!error)
        return std::unexpected(error.error());
    auto sent = send(bus, call, *error, {});
    if (!sent)
        return std::unexpected(sent.error());
    return std::optional<AgentAsk>{};
}

}
