// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/bt_addr.h"
#include "os/bt_device_path.h"
#include "os/bt_kind.h"
#include "os/bt_name.h"
#include "os/dbus_incoming.h"
#include "os/dbus_object_path.h"
#include "os/rssi_dbm.h"

namespace mister::os {

struct BluezObjectsAdapter {
    TASTY_SEAT_EXEMPT(component);

    DbusObjectPath path{};
    std::optional<BtAddr> address;
    std::optional<BtName> alias;
    std::optional<bool> powered;
    std::optional<bool> pairable;
    std::optional<bool> discovering;
    std::uint8_t props_truncated = 0;
};

struct BluezObjectsDevice {
    TASTY_SEAT_EXEMPT(component);

    BtDevicePath path{};
    std::optional<BtAddr> address;
    std::optional<BtName> alias;
    std::optional<BtKind> kind;
    std::optional<bool> paired;
    std::optional<bool> trusted;
    std::optional<bool> connected;
    std::optional<RssiDbm> rssi;
    std::optional<bool> hid;
    std::uint8_t props_truncated = 0;
};

class BluezObjects {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxAdapters = 2;
    static constexpr std::size_t kMaxDevices = 32;
    static constexpr std::string_view kObjectManagerInterface{"org.freedesktop.DBus.ObjectManager"};
    static constexpr std::string_view kPropertiesInterface{"org.freedesktop.DBus.Properties"};
    static constexpr std::string_view kAdapterInterface{"org.bluez.Adapter1"};
    static constexpr std::string_view kDeviceInterface{"org.bluez.Device1"};
    static constexpr std::string_view kManagedObjectsSignature{"a{oa{sa{sv}}}"};
    static constexpr std::string_view kInterfacesAddedSignature{"oa{sa{sv}}"};
    static constexpr std::string_view kInterfacesRemovedSignature{"oas"};
    static constexpr std::string_view kPropertiesChangedSignature{"sa{sv}as"};
    static constexpr std::string_view kHidUuid{"00001124-0000-1000-8000-00805f9b34fb"};
    static constexpr std::string_view kHidOverGattUuid{"00001812-0000-1000-8000-00805f9b34fb"};

    [[nodiscard]] static Ex<BluezObjects> from_managed_objects(
        const DbusIncoming& message
    ) noexcept;

    [[nodiscard]] static Ex<BluezObjects> from_interfaces_added(
        const DbusIncoming& message
    ) noexcept;

    [[nodiscard]] static Ex<BluezObjects> from_properties_changed(
        const DbusIncoming& message
    ) noexcept;

    [[nodiscard]] static Ex<DbusObjectPath> path_from_interfaces_removed(
        const DbusIncoming& message
    ) noexcept;

    [[nodiscard]] std::span<const BluezObjectsAdapter> adapters() const noexcept {
        return {adapters_.data(), adapter_count_};
    }

    [[nodiscard]] std::span<const BluezObjectsDevice> devices() const noexcept {
        return {devices_.data(), device_count_};
    }

    void note_interface_skipped() noexcept { ++interfaces_skipped_; }

    [[nodiscard]] std::size_t interfaces_skipped() const noexcept { return interfaces_skipped_; }

    [[nodiscard]] std::size_t rows_dropped() const noexcept { return rows_dropped_; }

private:
    std::array<BluezObjectsAdapter, kMaxAdapters> adapters_{};
    std::size_t adapter_count_ = 0;
    std::array<BluezObjectsDevice, kMaxDevices> devices_{};
    std::size_t device_count_ = 0;
    std::size_t interfaces_skipped_ = 0;
    std::size_t rows_dropped_ = 0;
};

static_assert(std::is_trivially_copyable_v<BluezObjectsAdapter>);
static_assert(std::is_trivially_copyable_v<BluezObjectsDevice>);
static_assert(std::is_trivially_copyable_v<BluezObjects>);

}
