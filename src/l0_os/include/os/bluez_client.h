// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/bt_device_path.h"
#include "os/dbus_connection.h"
#include "os/dbus_incoming.h"
#include "os/dbus_object_path.h"
#include "os/dbus_serial.h"

namespace mister::os {

class BluezClient {
    TASTY_SEAT_RESIDENT(Connect);

public:
    static constexpr std::string_view kService{"org.bluez"};
    static constexpr std::string_view kBusService{"org.freedesktop.DBus"};
    static constexpr std::string_view kBusPath{"/org/freedesktop/DBus"};
    static constexpr std::string_view kRootPath{"/"};
    static constexpr std::string_view kManagerPath{"/org/bluez"};
    static constexpr std::string_view kAgentPath{"/org/mister/agent"};
    static constexpr std::string_view kAgentCapability{"KeyboardDisplay"};
    static constexpr std::string_view kObjectManagerInterface{"org.freedesktop.DBus.ObjectManager"};
    static constexpr std::string_view kPropertiesInterface{"org.freedesktop.DBus.Properties"};
    static constexpr std::string_view kAgentManagerInterface{"org.bluez.AgentManager1"};
    static constexpr std::string_view kAdapterInterface{"org.bluez.Adapter1"};
    static constexpr std::string_view kDeviceInterface{"org.bluez.Device1"};
    static constexpr std::string_view kNameOwnerChangedRule{
        "type='signal',sender='org.freedesktop.DBus',"
        "interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.bluez'"
    };
    static constexpr std::string_view kObjectManagerRule{
        "type='signal',sender='org.bluez',"
        "interface='org.freedesktop.DBus.ObjectManager'"
    };
    static constexpr std::string_view kPropertiesRule{
        "type='signal',sender='org.bluez',"
        "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"
    };

    explicit BluezClient(DbusConnection& bus) noexcept : bus_(bus) {}

    [[nodiscard]] Ex<DbusSerial> add_object_manager_match() noexcept;
    [[nodiscard]] Ex<DbusSerial> add_properties_match() noexcept;
    [[nodiscard]] Ex<DbusSerial> watch_daemon() noexcept;
    [[nodiscard]] Ex<DbusSerial> name_has_owner() noexcept;
    [[nodiscard]] Ex<DbusSerial> get_managed_objects() noexcept;

    [[nodiscard]] Ex<DbusSerial> register_agent() noexcept;
    [[nodiscard]] Ex<DbusSerial> request_default_agent() noexcept;
    [[nodiscard]] Ex<DbusSerial> unregister_agent() noexcept;

    [[nodiscard]] Ex<DbusSerial> apply_discovery_filter(const DbusObjectPath& adapter) noexcept;
    [[nodiscard]] Ex<DbusSerial> start_discovery(const DbusObjectPath& adapter) noexcept;
    [[nodiscard]] Ex<DbusSerial> stop_discovery(const DbusObjectPath& adapter) noexcept;
    [[nodiscard]] Ex<DbusSerial> remove_device(
        const DbusObjectPath& adapter,
        const BtDevicePath& device
    ) noexcept;
    [[nodiscard]] Ex<DbusSerial> write_adapter_powered(
        const DbusObjectPath& adapter,
        bool powered
    ) noexcept;
    [[nodiscard]] Ex<DbusSerial> write_adapter_pairable(
        const DbusObjectPath& adapter,
        bool pairable
    ) noexcept;

    [[nodiscard]] Ex<DbusSerial> pair(const BtDevicePath& device) noexcept;
    [[nodiscard]] Ex<DbusSerial> cancel_pairing(const BtDevicePath& device) noexcept;
    [[nodiscard]] Ex<DbusSerial> connect_device(const BtDevicePath& device) noexcept;
    [[nodiscard]] Ex<DbusSerial> disconnect_device(const BtDevicePath& device) noexcept;
    [[nodiscard]] Ex<DbusSerial> write_device_trusted(const BtDevicePath& device) noexcept;

    [[nodiscard]] static Ex<bool> name_has_owner_reply(const DbusIncoming& message) noexcept;

private:
    [[nodiscard]] Ex<DbusSerial> send(
        std::string_view path,
        std::string_view interface_name,
        std::string_view member,
        std::string_view destination,
        std::string_view signature,
        std::span<const std::byte> body
    ) noexcept;
    [[nodiscard]] Ex<DbusSerial> set_property(
        std::string_view path,
        std::string_view interface_name,
        std::string_view property,
        bool value
    ) noexcept;

    DbusConnection& bus_;
};

}
