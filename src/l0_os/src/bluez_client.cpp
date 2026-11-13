// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/bluez_objects.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "dbus_wire_internal.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "os/bluez_client.h"
#include "os/dbus_header.h"
#include "os/dbus_message.h"
#include "os/dbus_reader.h"
#include "os/dbus_writer.h"
#include "os/types.h"

namespace mister::os {
namespace {

using namespace detail;

constexpr WireU32 kWireTrue{1};
constexpr WireU32 kWireFalse{0};

enum : std::size_t { kCallBodyMax = 256 };

constexpr std::string_view kTransportProperty{"Transport"};
constexpr std::string_view kTransportAuto{"auto"};

[[nodiscard]] Error format_error() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

class Cursor {
public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] BufferOffset position() const noexcept { return at_; }

    [[nodiscard]] WireBytes offset() const noexcept { return at_.since(BufferOffset::kStart); }

    [[nodiscard]] bool at_end() const noexcept { return at_ == end_of(bytes_); }

    [[nodiscard]] BufferOffset end() const noexcept { return end_of(bytes_); }

    bool need(WireBytes count) noexcept {
        if (!ok_)
            return false;
        if (count > left_in(bytes_, at_))
            return refuse();
        return true;
    }

    bool jump_to(BufferOffset offset) noexcept {
        if (offset > end_of(bytes_))
            return refuse();
        at_ = offset;
        return true;
    }

    bool align_to(WireBytes boundary) noexcept {
        if (boundary == WireBytes::kNone)
            return true;
        const WireBytes pad = padding_to(at_, boundary);
        if (!need(pad))
            return false;
        at_ = advance(at_, pad);
        return true;
    }

    [[nodiscard]] Ex<WireU8> u8() noexcept {
        if (!need(WireU8::kWidth))
            return std::unexpected(fail());
        const std::span<const std::byte> word = at_.at(bytes_);
        at_ = advance(at_, WireU8::kWidth);
        return WireU8::of_le(word);
    }

    [[nodiscard]] Ex<WireU16> u16() noexcept {
        if (!align_to(WireU16::kWidth) || !need(WireU16::kWidth))
            return std::unexpected(fail());
        const std::span<const std::byte> word = at_.at(bytes_);
        at_ = advance(at_, WireU16::kWidth);
        return WireU16::of_le(word);
    }

    [[nodiscard]] Ex<WireU32> u32() noexcept {
        if (!align_to(WireU32::kWidth) || !need(WireU32::kWidth))
            return std::unexpected(fail());
        const std::span<const std::byte> word = at_.at(bytes_);
        at_ = advance(at_, WireU32::kWidth);
        return WireU32::of_le(word);
    }

    [[nodiscard]] Ex<WireI16> i16() noexcept {
        const auto raw = u16();
        if (!raw)
            return std::unexpected(raw.error());
        return as_signed(*raw);
    }

    [[nodiscard]] Ex<bool> boolean() noexcept {
        const auto raw = u32();
        if (!raw)
            return std::unexpected(raw.error());
        if (*raw != kWireTrue && *raw != kWireFalse)
            return std::unexpected(fail());
        return *raw == kWireTrue;
    }

    [[nodiscard]] Ex<std::string_view> signature_text() noexcept {
        const auto length = u8();
        if (!length)
            return std::unexpected(length.error());
        return text_of(as_bytes(*length));
    }

    [[nodiscard]] Ex<std::string_view> string() noexcept {
        const auto length = u32();
        if (!length)
            return std::unexpected(length.error());
        return text_of(as_bytes(*length));
    }

    [[nodiscard]] Ex<DbusObjectPath> object_path() noexcept {
        const auto text = string();
        if (!text)
            return std::unexpected(text.error());
        const auto parsed = DbusObjectPath::parse(*text);
        if (!parsed)
            return std::unexpected(fail());
        return *parsed;
    }

    bool skip_bytes(WireBytes count) noexcept {
        if (!need(count))
            return false;
        at_ = advance(at_, count);
        return true;
    }

    bool skip_value(std::string_view signature) noexcept {
        if (signature.empty())
            return refuse();
        switch (signature.front()) {
            case kByte:
            case kBoolean:
                return skip_bytes(WireU8::kWidth);
            case kInt16:
            case kUint16:
                return align_to(WireU16::kWidth) && skip_bytes(WireU16::kWidth);
            case kInt32:
            case kUint32:
                return align_to(WireU32::kWidth) && skip_bytes(WireU32::kWidth);
            case kInt64:
            case kUint64:
                return align_to(WireU64::kWidth) && skip_bytes(WireU64::kWidth);
            case kString:
            case kObjectPath:
                return string().has_value();
            case kSignature:
                return signature_text().has_value();
            case kVariant: {
                const auto inner = signature_text();
                if (!inner)
                    return false;
                if (!single_complete_type(*inner))
                    return refuse();
                return skip_value(*inner);
            }
            case kArray: {
                const std::string_view element = signature.substr(1);
                if (!element_type_valid(element))
                    return refuse();
                const auto length = u32();
                if (!length)
                    return false;
                if (!align_to(align_of(element.front())))
                    return false;
                return skip_bytes(as_bytes(*length));
            }
            default:
                return refuse();
        }
    }

private:
    [[nodiscard]] Error fail() noexcept {
        ok_ = false;
        return format_error();
    }

    bool refuse() noexcept {
        ok_ = false;
        return false;
    }

    [[nodiscard]] Ex<std::string_view> text_of(WireBytes length) noexcept {
        if (!need(length + WireU8::kWidth))
            return std::unexpected(fail());
        const std::span<const std::byte> run = at_.at(bytes_);
        const std::span<const std::byte> body = length.head_of(run);
        if (length.tail_of(run).front() != std::byte{0})
            return std::unexpected(fail());
        at_ = advance(at_, length + WireU8::kWidth);
        return infra::as_text(body);
    }

    std::span<const std::byte> bytes_;
    BufferOffset at_{};
    bool ok_ = true;
};

bool decode_properties(Cursor& cursor, BufferOffset props_end, BluezObjectsAdapter& row) noexcept {
    while (cursor.position() < props_end) {
        if (!cursor.align_to(WireU64::kWidth))
            return false;
        const auto name = cursor.string();
        const auto signature = cursor.signature_text();
        if (!name || !signature)
            return false;
        if (!single_complete_type(*signature)) {
            row.props_truncated = 1;
            return cursor.jump_to(props_end);
        }
        if (*name == "Address" && *signature == "s") {
            const auto text = cursor.string();
            if (!text)
                return false;
            const auto address = BtAddr::parse(*text);
            if (!address)
                return false;
            row.address = *address;
        } else if (*name == "Alias" && *signature == "s") {
            const auto text = cursor.string();
            if (!text)
                return false;
            row.alias = BtName::of(*text);
        } else if (*name == "Powered" && *signature == "b") {
            const auto value = cursor.boolean();
            if (!value)
                return false;
            row.powered = *value;
        } else if (*name == "Pairable" && *signature == "b") {
            const auto value = cursor.boolean();
            if (!value)
                return false;
            row.pairable = *value;
        } else if (*name == "Discovering" && *signature == "b") {
            const auto value = cursor.boolean();
            if (!value)
                return false;
            row.discovering = *value;
        } else if (!cursor.skip_value(*signature)) {
            return false;
        }
    }
    return cursor.position() == props_end;
}

[[nodiscard]] Ex<bool> uuids_have_hid(Cursor& cursor) noexcept {
    const auto length = cursor.u32();
    if (!length)
        return std::unexpected(length.error());
    if (!cursor.align_to(WireU32::kWidth))
        return std::unexpected(format_error());
    const BufferOffset end = cursor.position() + as_bytes(*length);
    if (end > cursor.end())
        return std::unexpected(format_error());
    bool hid = false;
    while (cursor.position() < end) {
        const auto uuid = cursor.string();
        if (!uuid)
            return std::unexpected(uuid.error());
        if (*uuid == BluezObjects::kHidUuid || *uuid == BluezObjects::kHidOverGattUuid)
            hid = true;
    }
    if (cursor.position() != end)
        return std::unexpected(format_error());
    return hid;
}

bool decode_properties(Cursor& cursor, BufferOffset props_end, BluezObjectsDevice& row) noexcept {
    while (cursor.position() < props_end) {
        if (!cursor.align_to(WireU64::kWidth))
            return false;
        const auto name = cursor.string();
        const auto signature = cursor.signature_text();
        if (!name || !signature)
            return false;
        if (!single_complete_type(*signature)) {
            row.props_truncated = 1;
            return cursor.jump_to(props_end);
        }
        if (*name == "Address" && *signature == "s") {
            const auto text = cursor.string();
            if (!text)
                return false;
            const auto address = BtAddr::parse(*text);
            if (!address)
                return false;
            row.address = *address;
        } else if (*name == "Alias" && *signature == "s") {
            const auto text = cursor.string();
            if (!text)
                return false;
            row.alias = BtName::of(*text);
        } else if (*name == "Icon" && *signature == "s") {
            const auto text = cursor.string();
            if (!text)
                return false;
            row.kind = bt_kind_from_icon(*text);
        } else if (*name == "Paired" && *signature == "b") {
            const auto value = cursor.boolean();
            if (!value)
                return false;
            row.paired = *value;
        } else if (*name == "Trusted" && *signature == "b") {
            const auto value = cursor.boolean();
            if (!value)
                return false;
            row.trusted = *value;
        } else if (*name == "Connected" && *signature == "b") {
            const auto value = cursor.boolean();
            if (!value)
                return false;
            row.connected = *value;
        } else if (*name == "RSSI" && *signature == "n") {
            const auto dbm = cursor.i16();
            if (!dbm)
                return false;
            row.rssi = RssiDbm::of_wire(*dbm);
        } else if (*name == "UUIDs" && *signature == "as") {
            const auto hid = uuids_have_hid(cursor);
            if (!hid)
                return false;
            row.hid = *hid;
        } else if (!cursor.skip_value(*signature)) {
            return false;
        }
    }
    return cursor.position() == props_end;
}

void invalidate(BluezObjectsAdapter& row, std::string_view name) noexcept {
    if (name == "Address")
        row.address.reset();
    else if (name == "Alias")
        row.alias.reset();
    else if (name == "Powered")
        row.powered.reset();
    else if (name == "Pairable")
        row.pairable.reset();
    else if (name == "Discovering")
        row.discovering.reset();
}

void invalidate(BluezObjectsDevice& row, std::string_view name) noexcept {
    if (name == "Address")
        row.address.reset();
    else if (name == "Alias")
        row.alias.reset();
    else if (name == "Icon")
        row.kind.reset();
    else if (name == "Paired")
        row.paired.reset();
    else if (name == "Trusted")
        row.trusted.reset();
    else if (name == "Connected")
        row.connected.reset();
    else if (name == "RSSI")
        row.rssi.reset();
    else if (name == "UUIDs")
        row.hid.reset();
}

template <class Row>
bool decode_invalidated(Cursor& cursor, Row& row) noexcept {
    const auto length = cursor.u32();
    if (!length)
        return false;
    if (!cursor.align_to(WireU32::kWidth))
        return false;
    const BufferOffset end = cursor.position() + as_bytes(*length);
    if (end > cursor.end())
        return false;
    while (cursor.position() < end) {
        const auto name = cursor.string();
        if (!name)
            return false;
        invalidate(row, *name);
    }
    return cursor.position() == end;
}

[[nodiscard]] Ex<BluezObjects> failed(Cursor& cursor) noexcept {
    (void)cursor;
    return std::unexpected(format_error());
}

template <class Row>
[[nodiscard]] Ex<bool> decode_interfaces(
    Cursor& cursor,
    BufferOffset ifaces_end,
    std::string_view known,
    Row& row,
    BluezObjects& out
) noexcept {
    bool decoded = false;
    while (cursor.position() < ifaces_end) {
        if (!cursor.align_to(WireU64::kWidth))
            return std::unexpected(format_error());
        const auto interface_name = cursor.string();
        if (!interface_name)
            return std::unexpected(interface_name.error());
        const auto props_length = cursor.u32();
        if (!props_length)
            return std::unexpected(props_length.error());
        if (!cursor.align_to(WireU64::kWidth))
            return std::unexpected(format_error());
        const BufferOffset props_end = cursor.position() + as_bytes(*props_length);
        if (props_end > cursor.end())
            return std::unexpected(format_error());
        if (*interface_name == known) {
            if (!decode_properties(cursor, props_end, row)) {
                return std::unexpected(format_error());
            }
            decoded = true;
        } else {
            out.note_interface_skipped();
            if (!cursor.jump_to(props_end))
                return std::unexpected(format_error());
        }
        if (cursor.position() != props_end)
            return std::unexpected(format_error());
    }
    if (cursor.position() != ifaces_end)
        return std::unexpected(format_error());
    return decoded;
}

}

Ex<BluezObjects> BluezObjects::from_managed_objects(const DbusIncoming& message) noexcept {
    if (message.header.signature() != kManagedObjectsSignature) {
        return std::unexpected(format_error());
    }
    Cursor cursor{message.body};
    const auto total = cursor.u32();
    if (!total)
        return failed(cursor);
    if (!cursor.align_to(WireU64::kWidth))
        return failed(cursor);
    const BufferOffset objects_end = cursor.position() + as_bytes(*total);
    if (objects_end > cursor.end())
        return failed(cursor);

    BluezObjects out{};
    while (cursor.position() < objects_end) {
        if (!cursor.align_to(WireU64::kWidth))
            return failed(cursor);
        const auto path_text = cursor.string();
        if (!path_text)
            return failed(cursor);
        const auto interfaces_length = cursor.u32();
        if (!interfaces_length)
            return failed(cursor);
        if (!cursor.align_to(WireU64::kWidth))
            return failed(cursor);
        const BufferOffset interfaces_end = cursor.position() + as_bytes(*interfaces_length);
        if (interfaces_end > cursor.end())
            return failed(cursor);

        const auto device = BtDevicePath::parse(*path_text);
        const auto adapter_path = DbusObjectPath::parse(*path_text);
        if (!adapter_path)
            return failed(cursor);

        while (cursor.position() < interfaces_end) {
            if (!cursor.align_to(WireU64::kWidth))
                return failed(cursor);
            const auto interface_name = cursor.string();
            if (!interface_name)
                return failed(cursor);
            const auto props_length = cursor.u32();
            if (!props_length)
                return failed(cursor);
            if (!cursor.align_to(WireU64::kWidth))
                return failed(cursor);
            const BufferOffset props_end = cursor.position() + as_bytes(*props_length);
            if (props_end > cursor.end())
                return failed(cursor);

            if (*interface_name == kAdapterInterface) {
                if (out.adapter_count_ == kMaxAdapters) {
                    ++out.rows_dropped_;
                } else {
                    BluezObjectsAdapter& row = out.adapters_[out.adapter_count_];
                    row = BluezObjectsAdapter{};
                    row.path = *adapter_path;
                    if (!decode_properties(cursor, props_end, row))
                        return failed(cursor);
                    ++out.adapter_count_;
                }
            } else if (*interface_name == kDeviceInterface) {
                if (!device)
                    return failed(cursor);
                if (out.device_count_ == kMaxDevices) {
                    ++out.rows_dropped_;
                } else {
                    BluezObjectsDevice& row = out.devices_[out.device_count_];
                    row = BluezObjectsDevice{};
                    row.path = *device;
                    if (!decode_properties(cursor, props_end, row))
                        return failed(cursor);
                    ++out.device_count_;
                }
            } else {
                out.note_interface_skipped();
            }
            if (cursor.position() != props_end && !cursor.jump_to(props_end)) {
                return failed(cursor);
            }
        }
        if (cursor.position() != interfaces_end)
            return failed(cursor);
    }
    if (cursor.position() != objects_end || !cursor.at_end())
        return failed(cursor);
    return out;
}

Ex<BluezObjects> BluezObjects::from_interfaces_added(const DbusIncoming& message) noexcept {
    if (message.header.signature() != kInterfacesAddedSignature) {
        return std::unexpected(format_error());
    }
    Cursor cursor{message.body};
    const auto path_text = cursor.string();
    if (!path_text)
        return failed(cursor);
    const auto interfaces_length = cursor.u32();
    if (!interfaces_length)
        return failed(cursor);
    if (!cursor.align_to(WireU64::kWidth))
        return failed(cursor);
    const BufferOffset interfaces_end = cursor.position() + as_bytes(*interfaces_length);
    if (interfaces_end > cursor.end())
        return failed(cursor);

    BluezObjects out{};
    const auto device = BtDevicePath::parse(*path_text);
    if (device) {
        BluezObjectsDevice row{};
        row.path = *device;
        const auto decoded = decode_interfaces(cursor, interfaces_end, kDeviceInterface, row, out);
        if (!decoded)
            return failed(cursor);
        if (*decoded) {
            out.devices_[0] = row;
            out.device_count_ = 1;
        }
    } else {
        const auto adapter_path = DbusObjectPath::parse(*path_text);
        if (!adapter_path)
            return failed(cursor);
        BluezObjectsAdapter row{};
        row.path = *adapter_path;
        const auto decoded = decode_interfaces(cursor, interfaces_end, kAdapterInterface, row, out);
        if (!decoded)
            return failed(cursor);
        if (*decoded) {
            out.adapters_[0] = row;
            out.adapter_count_ = 1;
        }
    }
    if (cursor.position() != interfaces_end || !cursor.at_end())
        return failed(cursor);
    return out;
}

Ex<BluezObjects> BluezObjects::from_properties_changed(const DbusIncoming& message) noexcept {
    if (message.header.signature() != kPropertiesChangedSignature) {
        return std::unexpected(format_error());
    }
    Cursor cursor{message.body};
    const auto interface_name = cursor.string();
    if (!interface_name)
        return failed(cursor);
    const auto props_length = cursor.u32();
    if (!props_length)
        return failed(cursor);
    if (!cursor.align_to(WireU64::kWidth))
        return failed(cursor);
    const BufferOffset props_end = cursor.position() + as_bytes(*props_length);
    if (props_end > cursor.end())
        return failed(cursor);

    BluezObjects out{};
    if (*interface_name != kAdapterInterface && *interface_name != kDeviceInterface) {
        return out;
    }
    const auto adapter_path = DbusObjectPath::parse(message.header.path());
    if (!adapter_path)
        return failed(cursor);

    if (*interface_name == kAdapterInterface) {
        BluezObjectsAdapter row{};
        row.path = *adapter_path;
        if (!decode_properties(cursor, props_end, row))
            return failed(cursor);
        if (cursor.position() != props_end)
            return failed(cursor);
        if (!decode_invalidated(cursor, row))
            return failed(cursor);
        if (!cursor.at_end())
            return failed(cursor);
        out.adapters_[0] = row;
        out.adapter_count_ = 1;
        return out;
    }

    const auto device = BtDevicePath::parse(message.header.path());
    if (!device)
        return failed(cursor);
    BluezObjectsDevice row{};
    row.path = *device;
    if (!decode_properties(cursor, props_end, row))
        return failed(cursor);
    if (cursor.position() != props_end)
        return failed(cursor);
    if (!decode_invalidated(cursor, row))
        return failed(cursor);
    if (!cursor.at_end())
        return failed(cursor);
    out.devices_[0] = row;
    out.device_count_ = 1;
    return out;
}

Ex<DbusObjectPath> BluezObjects::path_from_interfaces_removed(
    const DbusIncoming& message
) noexcept {
    if (message.header.signature() != kInterfacesRemovedSignature) {
        return std::unexpected(format_error());
    }
    Cursor cursor{message.body};
    const auto path = cursor.object_path();
    if (!path)
        return std::unexpected(format_error());
    const auto length = cursor.u32();
    if (!length)
        return std::unexpected(format_error());
    if (!cursor.align_to(WireU32::kWidth))
        return std::unexpected(format_error());
    const BufferOffset end = cursor.position() + as_bytes(*length);
    if (end > cursor.end())
        return std::unexpected(format_error());
    while (cursor.position() < end) {
        const auto ignored = cursor.string();
        if (!ignored)
            return std::unexpected(format_error());
    }
    if (cursor.position() != end || !cursor.at_end())
        return std::unexpected(format_error());
    return *path;
}

Ex<DbusSerial> BluezClient::send(
    std::string_view path,
    std::string_view interface_name,
    std::string_view member,
    std::string_view destination,
    std::string_view signature,
    std::span<const std::byte> body
) noexcept {
    DbusHeader header{};
    header.type = DbusHeader::Type::MethodCall;
    auto added = header.add_field(DbusHeaderField::Code::Path, path);
    if (!added)
        return std::unexpected(added.error());
    added = header.add_field(DbusHeaderField::Code::Interface, interface_name);
    if (!added)
        return std::unexpected(added.error());
    added = header.add_field(DbusHeaderField::Code::Member, member);
    if (!added)
        return std::unexpected(added.error());
    added = header.add_field(DbusHeaderField::Code::Destination, destination);
    if (!added)
        return std::unexpected(added.error());
    if (!signature.empty()) {
        added = header.add_field(DbusHeaderField::Code::Signature, signature);
        if (!added)
            return std::unexpected(added.error());
    }
    const DbusMessage message{header, body};
    return bus_.call(message);
}

Ex<DbusSerial> BluezClient::add_object_manager_match() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kObjectManagerRule);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(kBusPath, kBusService, "AddMatch", kBusService, "s", writer.written());
}

Ex<DbusSerial> BluezClient::add_properties_match() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kPropertiesRule);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(kBusPath, kBusService, "AddMatch", kBusService, "s", writer.written());
}

Ex<DbusSerial> BluezClient::watch_daemon() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kNameOwnerChangedRule);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(kBusPath, kBusService, "AddMatch", kBusService, "s", writer.written());
}

Ex<DbusSerial> BluezClient::name_has_owner() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kService);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(kBusPath, kBusService, "NameHasOwner", kBusService, "s", writer.written());
}

Ex<DbusSerial> BluezClient::get_managed_objects() noexcept {
    return send(kRootPath, kObjectManagerInterface, "GetManagedObjects", kService, "", {});
}

Ex<DbusSerial> BluezClient::register_agent() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kAgentPath);
    writer.string(kAgentCapability);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(
        kManagerPath,
        kAgentManagerInterface,
        "RegisterAgent",
        kService,
        "os",
        writer.written()
    );
}

Ex<DbusSerial> BluezClient::request_default_agent() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kAgentPath);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(
        kManagerPath,
        kAgentManagerInterface,
        "RequestDefaultAgent",
        kService,
        "o",
        writer.written()
    );
}

Ex<DbusSerial> BluezClient::unregister_agent() noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(kAgentPath);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(
        kManagerPath,
        kAgentManagerInterface,
        "UnregisterAgent",
        kService,
        "o",
        writer.written()
    );
}

Ex<DbusSerial> BluezClient::apply_discovery_filter(const DbusObjectPath& adapter) noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    auto array = writer.begin_array("{sv}");
    if (!array)
        return std::unexpected(array.error());
    writer.begin_dict_entry();
    writer.string(kTransportProperty);
    auto variant = writer.begin_variant("s");
    if (!variant)
        return std::unexpected(variant.error());
    writer.string(kTransportAuto);
    writer.end_dict_entry();
    writer.end_array(*array);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(
        adapter.view(),
        kAdapterInterface,
        "SetDiscoveryFilter",
        kService,
        "a{sv}",
        writer.written()
    );
}

Ex<DbusSerial> BluezClient::start_discovery(const DbusObjectPath& adapter) noexcept {
    return send(adapter.view(), kAdapterInterface, "StartDiscovery", kService, "", {});
}

Ex<DbusSerial> BluezClient::stop_discovery(const DbusObjectPath& adapter) noexcept {
    return send(adapter.view(), kAdapterInterface, "StopDiscovery", kService, "", {});
}

Ex<DbusSerial> BluezClient::remove_device(
    const DbusObjectPath& adapter,
    const BtDevicePath& device
) noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(device.format().view());
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(adapter.view(), kAdapterInterface, "RemoveDevice", kService, "o", writer.written());
}

Ex<DbusSerial> BluezClient::set_property(
    std::string_view path,
    std::string_view interface_name,
    std::string_view property,
    bool value
) noexcept {
    std::array<std::byte, kCallBodyMax> arena{};
    DbusWriter writer{arena};
    writer.string(interface_name);
    writer.string(property);
    auto variant = writer.begin_variant("b");
    if (!variant)
        return std::unexpected(variant.error());
    writer.boolean(value);
    if (!writer.ok())
        return std::unexpected(writer.error());
    return send(path, kPropertiesInterface, "Set", kService, "ssv", writer.written());
}

Ex<DbusSerial> BluezClient::write_adapter_powered(
    const DbusObjectPath& adapter,
    bool powered
) noexcept {
    return set_property(adapter.view(), kAdapterInterface, "Powered", powered);
}

Ex<DbusSerial> BluezClient::write_adapter_pairable(
    const DbusObjectPath& adapter,
    bool pairable
) noexcept {
    return set_property(adapter.view(), kAdapterInterface, "Pairable", pairable);
}

Ex<DbusSerial> BluezClient::pair(const BtDevicePath& device) noexcept {
    return send(device.format().view(), kDeviceInterface, "Pair", kService, "", {});
}

Ex<DbusSerial> BluezClient::cancel_pairing(const BtDevicePath& device) noexcept {
    return send(device.format().view(), kDeviceInterface, "CancelPairing", kService, "", {});
}

Ex<DbusSerial> BluezClient::connect_device(const BtDevicePath& device) noexcept {
    return send(device.format().view(), kDeviceInterface, "Connect", kService, "", {});
}

Ex<DbusSerial> BluezClient::disconnect_device(const BtDevicePath& device) noexcept {
    return send(device.format().view(), kDeviceInterface, "Disconnect", kService, "", {});
}

Ex<DbusSerial> BluezClient::write_device_trusted(const BtDevicePath& device) noexcept {
    return set_property(device.format().view(), kDeviceInterface, "Trusted", true);
}

Ex<bool> BluezClient::name_has_owner_reply(const DbusIncoming& message) noexcept {
    if (message.header.signature() != "b")
        return std::unexpected(format_error());
    DbusReader reader{message.body, false};
    auto value = reader.boolean();
    if (!value)
        return std::unexpected(value.error());
    return *value;
}

}
