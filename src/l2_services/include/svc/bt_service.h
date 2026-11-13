// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "os/agent_ask.h"
#include "os/bluez_agent.h"
#include "os/bluez_client.h"
#include "os/bluez_objects.h"
#include "os/bt_radio.h"
#include "os/bt_addr.h"
#include "os/bt_device_path.h"
#include "os/bt_kind.h"
#include "os/dbus_connection.h"
#include "os/dbus_header.h"
#include "os/dbus_incoming.h"
#include "os/dbus_message.h"
#include "os/dbus_reader.h"
#include "os/dbus_serial.h"
#include "os/passkey.h"
#include "os/pin_code.h"
#include "os/raw_monotonic_clock.h"
#include "svc/bt_activity.h"
#include "svc/bt_device_view.h"
#include "svc/bt_failure.h"
#include "svc/bt_presence.h"
#include "svc/bt_radio_binding.h"
#include "svc/bt_prompt.h"
#include "svc/bt_prompt_view.h"
#include "svc/bt_view.h"
#include "svc/connect_fd_delta.h"
#include "svc/connect_request.h"
#include "svc/prompt_answer.h"
#include "svc/prompt_gen.h"
#include "svc/view_gen.h"

namespace mister::svc {

struct BusRetireDepth {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    friend constexpr bool operator==(BusRetireDepth, BusRetireDepth) = default;
};

inline constexpr BusRetireDepth kRetiringBuses{4};

struct TrackedCallDepth {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    friend constexpr bool operator==(TrackedCallDepth, TrackedCallDepth) = default;
};

inline constexpr TrackedCallDepth kTrackedCalls{12};

struct AdapterPlaces {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    [[nodiscard]] constexpr bool has_room(AdapterPlaces places) const noexcept {
        return v < places.v;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr AdapterPlaces next() const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v + 1);
        return AdapterPlaces{place};
    }

    friend constexpr bool operator==(AdapterPlaces, AdapterPlaces) = default;
};

inline constexpr AdapterPlaces kAdapterPlaces{os::BluezObjects::kMaxAdapters};

struct DevicePlaces {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    [[nodiscard]] constexpr bool has_room(DevicePlaces places) const noexcept {
        return v < places.v;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool covers(DevicePlaces place) const noexcept { return place.v < v; }

    [[nodiscard]] constexpr DevicePlaces next() const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v + 1);
        return DevicePlaces{place};
    }

    [[nodiscard]] constexpr DevicePlaces last() const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v - 1);
        return DevicePlaces{place};
    }

    friend constexpr bool operator==(DevicePlaces, DevicePlaces) = default;
};

inline constexpr DevicePlaces kDevicePlaces{os::BluezObjects::kMaxDevices};

template <class B>
concept BtBus = requires(
    B& bus,
    const B& const_bus,
    std::string_view path,
    const os::DbusMessage& message,
    const os::DbusIncoming& incoming
) {
    { B::kSystemBusSocket } -> std::convertible_to<std::string_view>;
    { B::open(path) } -> std::same_as<Ex<std::optional<B>>>;
    { const_bus.fd() } -> std::same_as<const UniqueFd*>;
    { bus.ready() } -> std::same_as<bool>;
    { bus.closed() } -> std::same_as<bool>;
    { bus.poll() } -> std::same_as<Ex<std::optional<os::DbusIncoming>>>;
    { bus.call(message) } -> std::same_as<Ex<os::DbusSerial>>;
    { bus.reply(incoming, message) } -> std::same_as<Ex<void>>;
    requires requires(B& b) { os::BluezClient{b}; };
};

template <class Bus, class Radio = os::BtRadio>
class BasicBtService {
    TASTY_SEAT_RESIDENT(Connect);

public:
    using Connection = Bus;

    struct ReplyArena {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr std::size_t size() const noexcept {
            const std::size_t bytes = v;
            return bytes;
        }

        friend constexpr bool operator==(ReplyArena, ReplyArena) = default;
    };

    struct PinSeed {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr PinSeed stepped(PinSeed stride) const noexcept {
            const std::uint32_t next = v * stride.v + 1;
            return PinSeed{next};
        }

        [[nodiscard]] constexpr os::Passkey shown() const noexcept {
            const std::uint32_t low = v % (os::Passkey::kMax + 1);
            return *os::Passkey::of(low);
        }

        friend constexpr bool operator==(PinSeed, PinSeed) = default;
    };

    static constexpr BusRetireDepth kRetireDepth = kRetiringBuses;
    static constexpr TrackedCallDepth kPendingDepth = kTrackedCalls;
    static constexpr ReplyArena kReplyArena{64};

    static constexpr PinSeed kPinStride{7919};
    static constexpr std::string_view kDefaultPin{"0000"};
    static constexpr std::chrono::seconds kPromptTimeout{30};
    static constexpr std::chrono::seconds kPairTimeout{60};

    static constexpr std::chrono::milliseconds kBootKickDelay{6000};

    static constexpr std::string_view kNameOwnerChanged{"NameOwnerChanged"};
    static constexpr std::string_view kInterfacesAdded{"InterfacesAdded"};
    static constexpr std::string_view kInterfacesRemoved{"InterfacesRemoved"};
    static constexpr std::string_view kPropertiesChanged{"PropertiesChanged"};
    static constexpr std::string_view kBlueZName{"org.bluez"};

    static constexpr std::string_view kAuthenticationFailed{"org.bluez.Error.AuthenticationFailed"};
    static constexpr std::string_view kAuthenticationRejected{
        "org.bluez.Error.AuthenticationRejected"
    };
    static constexpr std::string_view kAuthenticationCanceled{
        "org.bluez.Error.AuthenticationCanceled"
    };
    static constexpr std::string_view kConnectionAttemptFailed{
        "org.bluez.Error.ConnectionAttemptFailed"
    };
    static constexpr std::string_view kAlreadyExists{"org.bluez.Error.AlreadyExists"};

    explicit BasicBtService(
        std::string socket_path = std::string{Bus::kSystemBusSocket},
        Radio radio = Radio{}
    )
        : socket_path_(std::move(socket_path)),
          radio_(std::move(radio)) {}

    void on(const ConnectRequest::BtPage& request) noexcept {
        const bool was = pairing_page();
        page_ = request.mode;
        const bool now = pairing_page();
        auto_first_ = now && request.auto_first;
        discovery_wanted_ = now;
        if (!now) {
            auto_first_ = false;
            if (was)
                stop_discovery_go_ = true;
            drop_prompt();
        }
    }

    void on(const ConnectRequest::BtStopAutoFirst&) noexcept { auto_first_ = false; }

    void on(const ConnectRequest::BtPair& request) noexcept {
        auto_first_ = false;
        pair_go_ = request.addr;
    }

    void on(const ConnectRequest::BtCancelPair&) noexcept { cancel_pair_go_ = true; }

    void on(const ConnectRequest::BtConnect& request) noexcept { connect_go_ = request.addr; }

    void on(const ConnectRequest::BtDisconnect& request) noexcept { disconnect_go_ = request.addr; }

    void on(const ConnectRequest::BtRemove& request) noexcept { remove_go_ = request.addr; }

    void on(const ConnectRequest::BtRemoveAll&) noexcept { remove_all_go_ = true; }

    void on(const ConnectRequest::BtAnswer& request) noexcept {
        answer_ = Answer{request.gen, request.answer, request.passkey, request.pin};
    }

    void on(const ConnectRequest::BtResetAdapter&) noexcept { reset_adapter_go_ = true; }

    [[nodiscard]] ConnectFdDelta tick(os::RawMonotonicClock::time_point now) noexcept {
        ConnectFdDelta delta;
        run_radio(now);
        if (!bus_ && !retiring_full())
            open_bus(delta);
        if (bus_) {
            if (bus_->closed()) {
                retire_bus(delta);
            } else {
                drain(delta);
            }
        }
        if (bus_ && bus_->ready()) {
            begin_session();
            service(now);
        }
        publish();
        return delta;
    }

    void fds_removed() noexcept {
        for (auto& slot : retiring_buses_)
            slot.reset();
    }

    void stop() noexcept {
        if (bus_ && bus_->ready()) {
            if (discovery_running_ && !adapter_count_.is_empty()) {
                (void)client().stop_discovery(primary_adapter().path);
                discovery_running_ = false;
            }
            if (agent_registered_)
                (void)client().unregister_agent();
        }
        bus_.reset();
        reset_after_bus_loss();
        publish();
    }

    [[nodiscard]] const BtView& view() const noexcept { return view_; }

    [[nodiscard]] const BtDeviceView& device_view() const noexcept { return device_view_; }

    [[nodiscard]] const BtPromptView& prompt_view() const noexcept { return prompt_view_; }

    [[nodiscard]] const Radio& radio_for_test() const noexcept { return radio_; }

    static_assert(BtBus<Bus>, "the bus must satisfy the connection surface the clients speak");
    static_assert(
        BtRadioBinding<Radio>,
        "the radio must answer the probe, the reset and the cycle"
    );

private:
    enum class Op : std::uint8_t {
        Ignore,
        NameHasOwner,
        GetManagedObjects,
        RegisterAgent,
        RequestDefaultAgent,
        UnregisterAgent,
        Pair,
        Trusted,
        Connect,
        Disconnect,
        RemoveDevice,
        CancelPairing,
        StartDiscovery,
        StopDiscovery,
        SetFilter,
        kCount,
    };

    struct Pending {
        os::DbusSerial serial{};
        Op op = Op::Ignore;
        os::BtAddr addr{};
    };

    struct Answer {
        PromptGen gen{};
        PromptAnswer answer = PromptAnswer::Cancel;
        os::Passkey passkey = os::Passkey::zero();
        os::PinCode pin{};
    };

    struct PromptState {
        BtPrompt value{};
        bool held = false;
        os::DbusHeader caller{};
    };

    void run_radio(os::RawMonotonicClock::time_point now) noexcept {
        if (!boot_due_)
            boot_due_ = now + kBootKickDelay;
        if (!boot_spent_ && now >= *boot_due_) {
            boot_spent_ = true;
            const auto up = radio_.adapter_up();
            if (up && !*up)
                (void)radio_.cycle_usb();
        }
        if (reset_adapter_go_) {
            reset_adapter_go_ = false;
            (void)radio_.reset_adapter();
        }
    }

    [[nodiscard]] bool retiring_full() const noexcept {
        for (const auto& slot : retiring_buses_)
            if (!slot)
                return false;
        return true;
    }

    [[nodiscard]] ConnectFdDelta::Fd bus_fd_() const noexcept {
        const UniqueFd* fd = bus_->fd();
        return fd ? ConnectFdDelta::Fd{fd->get()} : ConnectFdDelta::Fd{};
    }

    void open_bus(ConnectFdDelta& delta) noexcept {
        auto opened = Bus::open(socket_path_);
        if (!opened) {
            ++open_errors_;
            return;
        }
        if (!opened->has_value())
            return;
        bus_.emplace(std::move(**opened));
        const ConnectFdDelta::Fd fd = bus_fd_();
        if (fd.open())
            delta.add(fd);
        presence_ = BtPresence::NoDaemon;
    }

    void retire_bus(ConnectFdDelta& delta) noexcept {
        if (!bus_)
            return;
        const ConnectFdDelta::Fd fd = bus_fd_();
        for (auto& slot : retiring_buses_) {
            if (slot)
                continue;
            if (fd.open())
                delta.remove(fd);
            slot.emplace(std::move(*bus_));
            bus_.reset();
            reset_after_bus_loss();
            return;
        }
        ++retire_refusals_;
    }

    void reset_after_bus_loss() noexcept {
        started_ = false;
        daemon_up_ = false;
        agent_registered_ = false;
        discovery_running_ = false;
        adapter_count_ = AdapterPlaces{};
        device_count_ = DevicePlaces{};
        pending_ = {};
        prompt_.reset();
        prompt_at_.reset();
        answer_.reset();
        clear_pair();
        presence_ = BtPresence::NoBus;
        activity_ = BtActivity::idle();
    }

    [[nodiscard]] os::BluezClient client() noexcept { return os::BluezClient{*bus_}; }

    void track(Ex<os::DbusSerial> serial, Op op, os::BtAddr addr = {}) noexcept {
        if (!serial) {
            ++send_errors_;
            return;
        }
        for (auto& slot : pending_) {
            if (slot)
                continue;
            slot = Pending{*serial, op, addr};
            return;
        }
        ++send_errors_;
    }

    void drain(ConnectFdDelta& delta) noexcept {
        for (;;) {
            auto message = bus_->poll();
            if (!message) {
                retire_bus(delta);
                return;
            }
            if (!message->has_value())
                return;
            handle_message(**message);
            if (!bus_)
                return;
        }
    }

    void handle_message(const os::DbusIncoming& message) noexcept {
        switch (message.header.type) {
            case os::DbusHeader::Type::MethodReturn:
                handle_reply(message, false);
                break;
            case os::DbusHeader::Type::Error:
                handle_reply(message, true);
                break;
            case os::DbusHeader::Type::Signal:
                handle_signal(message);
                break;
            case os::DbusHeader::Type::MethodCall:
                handle_call(message);
                break;
            case os::DbusHeader::Type::kCount:
                break;
        }
    }

    void handle_reply(const os::DbusIncoming& message, bool is_error) noexcept {
        const os::DbusSerial serial = message.header.reply_serial();
        if (serial.value() == 0)
            return;
        for (auto& slot : pending_) {
            if (!slot || !(slot->serial == serial))
                continue;
            const Pending pending = *slot;
            slot.reset();
            apply_reply(pending, message, is_error);
            return;
        }
    }

    void apply_reply(
        const Pending& pending,
        const os::DbusIncoming& message,
        bool is_error
    ) noexcept {
        switch (pending.op) {
            case Op::NameHasOwner:
                apply_name_has_owner(message, is_error);
                break;
            case Op::GetManagedObjects:
                apply_managed_objects(message, is_error);
                break;
            case Op::RegisterAgent:
                if (!is_error)
                    agent_registered_ = true;
                break;
            case Op::Pair:
                apply_pair(message, is_error, pending.addr);
                break;
            case Op::Trusted:
                apply_trusted(message, is_error, pending.addr);
                break;
            case Op::Connect:
                apply_connect(message, is_error, pending.addr);
                break;
            case Op::Disconnect:
                if (!is_error)
                    set_connected(pending.addr, false);
                break;
            case Op::RemoveDevice:
                if (!is_error)
                    drop_device(pending.addr);
                break;
            case Op::CancelPairing:
                break;
            case Op::StartDiscovery:
                discovery_running_ = !is_error;
                if (discovery_running_ && !activity_.busy()) {
                    activity_ = BtActivity::discovering();
                }
                break;
            case Op::StopDiscovery:
                discovery_running_ = false;
                if (!activity_.busy())
                    activity_ = BtActivity::idle();
                break;
            case Op::SetFilter:
            case Op::RequestDefaultAgent:
            case Op::UnregisterAgent:
            case Op::Ignore:
            case Op::kCount:
                break;
        }
    }

    void apply_name_has_owner(const os::DbusIncoming& message, bool is_error) noexcept {
        if (is_error) {
            daemon_down();
            return;
        }
        const auto owned = os::BluezClient::name_has_owner_reply(message);
        if (!owned || !*owned) {
            daemon_down();
            return;
        }
        daemon_up_ = true;
        request_objects();
        (void)track(client().register_agent(), Op::RegisterAgent);
        (void)track(client().request_default_agent(), Op::RequestDefaultAgent);
    }

    void apply_managed_objects(const os::DbusIncoming& message, bool is_error) noexcept {
        if (is_error)
            return;
        auto objects = os::BluezObjects::from_managed_objects(message);
        if (!objects) {
            ++decode_errors_;
            return;
        }
        adapter_count_ = AdapterPlaces{};
        device_count_ = DevicePlaces{};
        for (const auto& adapter : objects->adapters()) {
            if (!adapter_count_.has_room(kAdapterPlaces))
                break;
            adapters_[adapter_count_] = adapter;
            adapter_count_ = adapter_count_.next();
        }
        for (const auto& device : objects->devices()) {
            if (!device_count_.has_room(kDevicePlaces))
                break;
            devices_[device_count_] = device;
            device_count_ = device_count_.next();
        }
        update_presence();
        maybe_auto_first();
    }

    void apply_pair(const os::DbusIncoming& message, bool is_error, os::BtAddr addr) noexcept {
        if (is_error) {
            const std::string_view name = message.header.error_name();
            if (name == kAlreadyExists) {
                pair_succeeded(addr);
                return;
            }
            failure_ = failure_from(name);
            clear_pair();
            return;
        }
        pair_succeeded(addr);
    }

    void pair_succeeded(os::BtAddr addr) noexcept {
        set_paired(addr, true);
        if (auto path = device_path(addr)) {
            (void)track(client().write_device_trusted(*path), Op::Trusted, addr);
        } else {
            clear_pair();
        }
    }

    void apply_trusted(const os::DbusIncoming& message, bool is_error, os::BtAddr addr) noexcept {
        if (is_error) {
            failure_ = failure_from(message.header.error_name());
            clear_pair();
            return;
        }
        set_trusted(addr, true);
        if (auto path = device_path(addr)) {
            activity_ = BtActivity::connecting(addr);
            (void)track(client().connect_device(*path), Op::Connect, addr);
        } else {
            clear_pair();
        }
    }

    void apply_connect(const os::DbusIncoming& message, bool is_error, os::BtAddr addr) noexcept {
        if (is_error) {
            failure_ = failure_from(message.header.error_name());
            clear_pair();
            return;
        }
        set_connected(addr, true);
        failure_ = BtFailure::None;
        clear_pair();
    }

    [[nodiscard]] static BtFailure failure_from(std::string_view name) noexcept {
        if (name == kAuthenticationFailed)
            return BtFailure::AuthenticationFailed;
        if (name == kAuthenticationRejected)
            return BtFailure::AuthenticationRejected;
        if (name == kAuthenticationCanceled)
            return BtFailure::AuthenticationCanceled;
        if (name == kConnectionAttemptFailed)
            return BtFailure::ConnectionAttemptFailed;
        return BtFailure::Failed;
    }

    void handle_signal(const os::DbusIncoming& message) noexcept {
        const std::string_view interface_name = message.header.interface_name();
        const std::string_view member = message.header.member();
        if (interface_name == os::BluezClient::kBusService && member == kNameOwnerChanged) {
            apply_name_owner_changed(message);
            return;
        }
        if (interface_name == os::BluezObjects::kObjectManagerInterface) {
            if (member == kInterfacesAdded)
                merge_added(message);
            if (member == kInterfacesRemoved)
                merge_removed(message);
            return;
        }
        if (interface_name == os::BluezObjects::kPropertiesInterface
            && member == kPropertiesChanged) {
            merge_changed(message);
        }
    }

    void apply_name_owner_changed(const os::DbusIncoming& message) noexcept {
        os::DbusReader reader{message.body, false};
        auto name = reader.string();
        auto old_owner = reader.string();
        auto new_owner = reader.string();
        if (!name || !old_owner || !new_owner)
            return;
        if (*name != kBlueZName)
            return;
        if (new_owner->empty()) {
            daemon_down();
            return;
        }
        daemon_up_ = true;
        request_objects();
        (void)track(client().register_agent(), Op::RegisterAgent);
    }

    void daemon_down() noexcept {
        daemon_up_ = false;
        agent_registered_ = false;
        discovery_running_ = false;
        adapter_count_ = AdapterPlaces{};
        device_count_ = DevicePlaces{};
        prompt_.reset();
        prompt_at_.reset();
        clear_pair();
        presence_ = BtPresence::NoDaemon;
        activity_ = BtActivity::idle();
    }

    void request_objects() noexcept {
        (void)track(client().get_managed_objects(), Op::GetManagedObjects);
    }

    void merge_added(const os::DbusIncoming& message) noexcept {
        auto added = os::BluezObjects::from_interfaces_added(message);
        if (!added) {
            ++decode_errors_;
            return;
        }
        merge(*added);
        if (repair_after_remove_) {
            const os::BtAddr repair = *repair_after_remove_;
            for (const auto& device : added->devices()) {
                if (!(device.path.addr() == repair))
                    continue;
                repair_after_remove_.reset();
                start_pair(repair);
                break;
            }
        }
        update_presence();
        maybe_auto_first();
    }

    void merge_removed(const os::DbusIncoming& message) noexcept {
        auto path = os::BluezObjects::path_from_interfaces_removed(message);
        if (!path) {
            ++decode_errors_;
            return;
        }
        auto device = os::BtDevicePath::parse(path->view());
        if (device)
            drop_device(*device);
        update_presence();
    }

    void merge_changed(const os::DbusIncoming& message) noexcept {
        auto changed = os::BluezObjects::from_properties_changed(message);
        if (!changed) {
            ++decode_errors_;
            return;
        }
        merge(*changed);
        update_presence();
    }

    void merge(const os::BluezObjects& objects) noexcept {
        for (const auto& adapter : objects.adapters()) {
            if (auto* slot = find_adapter(adapter.path)) {
                merge_adapter(*slot, adapter);
            } else if (adapter_count_.has_room(kAdapterPlaces)) {
                adapters_[adapter_count_] = adapter;
                adapter_count_ = adapter_count_.next();
            }
        }
        for (const auto& device : objects.devices()) {
            if (auto* slot = find_device(device.path)) {
                merge_device(*slot, device);
            } else if (device_count_.has_room(kDevicePlaces)) {
                devices_[device_count_] = device;
                device_count_ = device_count_.next();
            }
        }
    }

    static void merge_adapter(
        os::BluezObjectsAdapter& into,
        const os::BluezObjectsAdapter& from
    ) noexcept {
        if (from.address)
            into.address = from.address;
        if (from.alias)
            into.alias = from.alias;
        if (from.powered)
            into.powered = from.powered;
        if (from.pairable)
            into.pairable = from.pairable;
        if (from.discovering)
            into.discovering = from.discovering;
    }

    static void merge_device(
        os::BluezObjectsDevice& into,
        const os::BluezObjectsDevice& from
    ) noexcept {
        if (from.address)
            into.address = from.address;
        if (from.alias)
            into.alias = from.alias;
        if (from.kind)
            into.kind = from.kind;
        if (from.paired)
            into.paired = from.paired;
        if (from.trusted)
            into.trusted = from.trusted;
        if (from.connected)
            into.connected = from.connected;
        if (from.rssi)
            into.rssi = from.rssi;
        if (from.hid)
            into.hid = from.hid;
    }

    void handle_call(const os::DbusIncoming& message) noexcept {
        auto ask = os::BluezAgent::handle(*bus_, message);
        if (!ask) {
            ++agent_errors_;
            return;
        }
        if (!ask->has_value())
            return;
        apply_ask(message, **ask);
    }

    void apply_ask(const os::DbusIncoming& call, const os::AgentAsk& ask) noexcept {
        if (ask.kind == os::AgentAsk::Kind::Release) {
            accept_call(call);
            return;
        }
        if (ask.kind == os::AgentAsk::Kind::Cancel) {
            drop_prompt();
            accept_call(call);
            return;
        }
        const auto device = ask_device(ask);
        if (!device)
            return;
        const os::BtAddr addr = device->addr();
        const bool in_session = pairing_page();
        const bool pairing_this = activity_.device() == addr;
        const bool known_trusted = trusted(addr);

        if (ask.kind == os::AgentAsk::Kind::AuthorizeService) {
            if ((in_session && pairing_this) || known_trusted) {
                accept_call(call);
                return;
            }
            if (!in_session) {
                reject_call(call);
                return;
            }
            show_authorize(call, addr);
            return;
        }
        if (ask.kind == os::AgentAsk::Kind::RequestAuthorization) {
            if (!in_session) {
                reject_call(call);
                return;
            }
            if (pairing_this) {
                accept_call(call);
                return;
            }
            show_authorize(call, addr);
            return;
        }
        if (!in_session) {
            reject_call(call);
            return;
        }
        switch (ask.kind) {
            case os::AgentAsk::Kind::RequestPinCode: {
                if (device_kind(addr) == os::BtKind::Keyboard) {
                    const os::PinCode pin = fresh_pin();
                    show_pin(call, addr, pin);
                    send_pin(call, pin.view());
                    return;
                }
                send_pin(call, kDefaultPin);
                return;
            }
            case os::AgentAsk::Kind::DisplayPinCode: {
                const auto shown = infra::as<os::AgentAsk::DisplayPinCode>(ask);
                if (shown)
                    show_pin(call, addr, shown->pin);
                accept_call(call);
                return;
            }
            case os::AgentAsk::Kind::DisplayPasskey: {
                const auto shown = infra::as<os::AgentAsk::DisplayPasskey>(ask);
                if (shown)
                    show_passkey(addr, shown->passkey, shown->digits);
                accept_call(call);
                return;
            }
            case os::AgentAsk::Kind::RequestPasskey:
                show_prompt(
                    call,
                    infra::make<BtPrompt>(BtPrompt::EnterPasskey{next_gen(), addr, {}}),
                    true
                );
                return;
            case os::AgentAsk::Kind::RequestConfirmation: {
                const auto shown = infra::as<os::AgentAsk::RequestConfirmation>(ask);
                if (!shown) {
                    reject_call(call);
                    return;
                }
                show_prompt(
                    call,
                    infra::make<BtPrompt>(BtPrompt::Confirm{next_gen(), shown->passkey, addr, {}}),
                    true
                );
                return;
            }
            case os::AgentAsk::Kind::RequestAuthorization:
            case os::AgentAsk::Kind::AuthorizeService:
            case os::AgentAsk::Kind::Release:
            case os::AgentAsk::Kind::Cancel:
            case os::AgentAsk::Kind::kCount:
                break;
        }
    }

    [[nodiscard]] static std::optional<os::BtDevicePath> ask_device(
        const os::AgentAsk& ask
    ) noexcept {
        switch (ask.kind) {
            case os::AgentAsk::Kind::RequestPinCode:
                return device_of<os::AgentAsk::RequestPinCode>(ask);
            case os::AgentAsk::Kind::DisplayPinCode:
                return device_of<os::AgentAsk::DisplayPinCode>(ask);
            case os::AgentAsk::Kind::RequestPasskey:
                return device_of<os::AgentAsk::RequestPasskey>(ask);
            case os::AgentAsk::Kind::DisplayPasskey:
                return device_of<os::AgentAsk::DisplayPasskey>(ask);
            case os::AgentAsk::Kind::RequestConfirmation:
                return device_of<os::AgentAsk::RequestConfirmation>(ask);
            case os::AgentAsk::Kind::RequestAuthorization:
                return device_of<os::AgentAsk::RequestAuthorization>(ask);
            case os::AgentAsk::Kind::AuthorizeService:
                return device_of<os::AgentAsk::AuthorizeService>(ask);
            case os::AgentAsk::Kind::Release:
            case os::AgentAsk::Kind::Cancel:
            case os::AgentAsk::Kind::kCount:
                break;
        }
        return std::nullopt;
    }

    template <class A>
    [[nodiscard]] static std::optional<os::BtDevicePath> device_of(
        const os::AgentAsk& ask
    ) noexcept {
        const auto alternative = infra::as<A>(ask);
        if (!alternative)
            return std::nullopt;
        return alternative->device;
    }

    void show_authorize(const os::DbusIncoming& call, os::BtAddr addr) noexcept {
        show_prompt(
            call,
            infra::make<BtPrompt>(BtPrompt::Authorize{next_gen(), addr, device_kind(addr), {}}),
            true
        );
    }

    void show_pin(const os::DbusIncoming& call, os::BtAddr addr, os::PinCode pin) noexcept {
        show_prompt(
            call,
            infra::make<BtPrompt>(BtPrompt::ShowPin{next_gen(), addr, pin, {}}),
            false
        );
    }

    void show_passkey(os::BtAddr addr, os::Passkey passkey, os::PasskeyDigits digits) noexcept {
        show_prompt(
            os::DbusIncoming{},
            infra::make<BtPrompt>(BtPrompt::ShowPasskey{next_gen(), passkey, addr, digits, {}}),
            false
        );
    }

    void show_prompt(const os::DbusIncoming& call, BtPrompt prompt, bool held) noexcept {
        if (prompt_ && prompt_->held)
            send_held(*prompt_, PromptAnswer::Cancel);
        PromptState state{};
        state.value = prompt;
        state.held = held;
        if (held)
            state.caller = call.header;
        prompt_ = state;
        prompt_at_.reset();
    }

    [[nodiscard]] PromptGen next_gen() noexcept {
        next_gen_ = next_gen_.next();
        return next_gen_;
    }

    [[nodiscard]] os::PinCode fresh_pin() noexcept {
        pin_seed_ = pin_seed_.stepped(kPinStride);
        const auto pin = os::PinCode::parse(pin_seed_.shown().format6().view());
        if (!pin)
            return *os::PinCode::parse(kDefaultPin);
        return *pin;
    }

    void accept_call(const os::DbusIncoming& call) noexcept {
        auto header = os::BluezAgent::accepted_reply(call);
        if (header)
            (void)os::BluezAgent::send(*bus_, call, *header, {});
    }

    void reject_call(const os::DbusIncoming& call) noexcept {
        auto header = os::BluezAgent::rejected_reply(call);
        if (header)
            (void)os::BluezAgent::send(*bus_, call, *header, {});
    }

    void cancel_call(const os::DbusIncoming& call) noexcept {
        auto header = os::BluezAgent::canceled_reply(call);
        if (header)
            (void)os::BluezAgent::send(*bus_, call, *header, {});
    }

    void send_pin(const os::DbusIncoming& call, std::string_view pin) noexcept {
        auto header = os::BluezAgent::pin_reply(call);
        if (!header)
            return;
        std::array<std::byte, kReplyArena.size()> arena{};
        auto body = os::BluezAgent::encode_pin(pin, arena);
        if (!body)
            return;
        (void)os::BluezAgent::send(*bus_, call, *header, *body);
    }

    void send_passkey(const os::DbusIncoming& call, os::Passkey passkey) noexcept {
        auto header = os::BluezAgent::passkey_reply(call);
        if (!header)
            return;
        std::array<std::byte, kReplyArena.size()> arena{};
        auto body = os::BluezAgent::encode_passkey(passkey, arena);
        if (!body)
            return;
        (void)os::BluezAgent::send(*bus_, call, *header, *body);
    }

    void answer_prompt(
        const PromptState& state,
        PromptAnswer answer,
        const Answer& typed
    ) noexcept {
        os::DbusIncoming call{state.caller, {}};
        switch (state.value.kind) {
            case BtPrompt::Kind::Confirm:
            case BtPrompt::Kind::Authorize:
                if (answer == PromptAnswer::Accept)
                    accept_call(call);
                else if (answer == PromptAnswer::Reject)
                    reject_call(call);
                else
                    cancel_call(call);
                break;
            case BtPrompt::Kind::EnterPasskey:
                if (answer == PromptAnswer::Accept)
                    send_passkey(call, typed.passkey);
                else if (answer == PromptAnswer::Reject)
                    reject_call(call);
                else
                    cancel_call(call);
                break;
            case BtPrompt::Kind::EnterPin:
                if (answer == PromptAnswer::Accept)
                    send_pin(call, typed.pin.size() == 0 ? kDefaultPin : typed.pin.view());
                else if (answer == PromptAnswer::Reject)
                    reject_call(call);
                else
                    cancel_call(call);
                break;
            case BtPrompt::Kind::ShowPasskey:
            case BtPrompt::Kind::ShowPin:
            case BtPrompt::Kind::kCount:
                break;
        }
    }

    void send_held(
        const PromptState& state,
        PromptAnswer answer,
        const Answer& typed = Answer{}
    ) noexcept {
        if (!state.held || !bus_)
            return;
        answer_prompt(state, answer, typed);
    }

    void drop_prompt() noexcept {
        if (!prompt_)
            return;
        send_held(*prompt_, PromptAnswer::Cancel);
        prompt_.reset();
        prompt_at_.reset();
    }

    [[nodiscard]] static PromptGen prompt_gen(const BtPrompt& prompt) noexcept {
        switch (prompt.kind) {
            case BtPrompt::Kind::ShowPasskey:
                return gen_of<BtPrompt::ShowPasskey>(prompt);
            case BtPrompt::Kind::ShowPin:
                return gen_of<BtPrompt::ShowPin>(prompt);
            case BtPrompt::Kind::Confirm:
                return gen_of<BtPrompt::Confirm>(prompt);
            case BtPrompt::Kind::EnterPasskey:
                return gen_of<BtPrompt::EnterPasskey>(prompt);
            case BtPrompt::Kind::EnterPin:
                return gen_of<BtPrompt::EnterPin>(prompt);
            case BtPrompt::Kind::Authorize:
                return gen_of<BtPrompt::Authorize>(prompt);
            case BtPrompt::Kind::kCount:
                break;
        }
        return PromptGen{};
    }

    template <class A>
    [[nodiscard]] static PromptGen gen_of(const BtPrompt& prompt) noexcept {
        const auto alternative = infra::as<A>(prompt);
        if (!alternative)
            return PromptGen{};
        return alternative->gen;
    }

    void flush_answer() noexcept {
        if (!answer_)
            return;
        const Answer answer = *answer_;
        answer_.reset();
        if (!prompt_ || !(prompt_gen(prompt_->value) == answer.gen))
            return;
        const PromptState state = *prompt_;
        prompt_.reset();
        prompt_at_.reset();
        send_held(state, answer.answer, answer);
    }

    void begin_session() noexcept {
        if (started_)
            return;
        started_ = true;
        (void)track(client().add_object_manager_match(), Op::Ignore);
        (void)track(client().add_properties_match(), Op::Ignore);
        (void)track(client().watch_daemon(), Op::Ignore);
        (void)track(client().name_has_owner(), Op::NameHasOwner);
    }

    void service(os::RawMonotonicClock::time_point now) noexcept {
        if (prompt_ && !prompt_at_)
            prompt_at_ = now;
        if (pair_addr_ && !pair_started_)
            pair_started_ = now;
        flush_answer();
        check_deadlines(now);
        if (!bus_)
            return;
        maybe_auto_first();
        issue_intents();
    }

    void check_deadlines(os::RawMonotonicClock::time_point now) noexcept {
        if (prompt_ && prompt_at_ && now - *prompt_at_ >= kPromptTimeout)
            drop_prompt();
        if (!pair_addr_ || !pair_started_)
            return;
        if (now - *pair_started_ < kPairTimeout)
            return;
        failure_ = BtFailure::PairTimedOut;
        if (auto path = device_path(*pair_addr_)) {
            (void)track(client().cancel_pairing(*path), Op::CancelPairing);
        }
        clear_pair();
    }

    void issue_intents() noexcept {
        if (stop_discovery_go_) {
            stop_discovery_go_ = false;
            stop_discovery();
        }
        if (discovery_wanted_ && pairing_page() && !discovery_running_)
            start_discovery();
        if (cancel_pair_go_) {
            cancel_pair_go_ = false;
            cancel_pair();
        }
        if (pair_go_) {
            const os::BtAddr addr = *pair_go_;
            pair_go_.reset();
            begin_pair(addr);
        }
        if (connect_go_) {
            const os::BtAddr addr = *connect_go_;
            connect_go_.reset();
            begin_connect(addr);
        }
        if (disconnect_go_) {
            const os::BtAddr addr = *disconnect_go_;
            disconnect_go_.reset();
            if (auto path = device_path(addr)) {
                (void)track(client().disconnect_device(*path), Op::Disconnect, addr);
            }
        }
        if (remove_go_) {
            const os::BtAddr addr = *remove_go_;
            remove_go_.reset();
            remove_device(addr);
        }
        if (remove_all_go_) {
            remove_all_go_ = false;
            remove_all_active_ = true;
        }
        pump_remove_all();
    }

    [[nodiscard]] bool pairing_page() const noexcept {
        return page_ == BtPageMode::Pairing || page_ == BtPageMode::PairingAll;
    }

    void start_discovery() noexcept {
        if (!pairing_page() || presence_ != BtPresence::Ready)
            return;
        if (adapter_count_.is_empty() || discovery_running_)
            return;
        const auto adapter = primary_adapter().path;
        (void)track(client().apply_discovery_filter(adapter), Op::SetFilter);
        (void)track(client().start_discovery(adapter), Op::StartDiscovery);
        discovery_running_ = true;
        if (!activity_.busy())
            activity_ = BtActivity::discovering();
    }

    void stop_discovery() noexcept {
        if (!discovery_running_)
            return;
        discovery_running_ = false;
        if (!adapter_count_.is_empty()) {
            (void)track(client().stop_discovery(primary_adapter().path), Op::StopDiscovery);
        }
        if (!activity_.busy())
            activity_ = BtActivity::idle();
    }

    void cancel_pair() noexcept {
        if (!pair_addr_)
            return;
        if (auto path = device_path(*pair_addr_)) {
            (void)track(client().cancel_pairing(*path), Op::CancelPairing);
        }
        clear_pair();
    }

    void begin_pair(os::BtAddr addr) noexcept {
        if (!daemon_up_ || presence_ != BtPresence::Ready) {
            failure_ = BtFailure::Failed;
            return;
        }
        failure_ = BtFailure::None;
        if (const auto* device = find_device(addr); device && device->paired && *device->paired) {
            activity_ = BtActivity::pairing(addr);
            pair_addr_ = addr;
            pair_started_.reset();
            repair_after_remove_ = addr;
            (void)track(
                client().remove_device(primary_adapter().path, device->path),
                Op::RemoveDevice,
                addr
            );
            return;
        }
        start_pair(addr);
    }

    void start_pair(os::BtAddr addr) noexcept {
        auto path = device_path(addr);
        if (!path) {
            failure_ = BtFailure::Failed;
            return;
        }
        activity_ = BtActivity::pairing(addr);
        pair_addr_ = addr;
        pair_started_.reset();
        failure_ = BtFailure::None;
        (void)track(client().pair(*path), Op::Pair, addr);
    }

    void begin_connect(os::BtAddr addr) noexcept {
        auto path = device_path(addr);
        if (!path) {
            failure_ = BtFailure::Failed;
            return;
        }
        activity_ = BtActivity::connecting(addr);
        failure_ = BtFailure::None;
        (void)track(client().connect_device(*path), Op::Connect, addr);
    }

    void remove_device(os::BtAddr addr) noexcept {
        auto path = device_path(addr);
        if (!path || adapter_count_.is_empty()) {
            failure_ = BtFailure::Failed;
            return;
        }
        (void)track(client().remove_device(primary_adapter().path, *path), Op::RemoveDevice, addr);
    }

    [[nodiscard]] bool remove_in_flight() const noexcept {
        for (const auto& slot : pending_)
            if (slot && slot->op == Op::RemoveDevice)
                return true;
        return false;
    }

    void pump_remove_all() noexcept {
        if (!remove_all_active_ || remove_in_flight())
            return;
        for (const auto& device : devices_.prefix(device_count_)) {
            if (!(device.paired && *device.paired))
                continue;
            if (adapter_count_.is_empty())
                break;
            (void)track(
                client().remove_device(primary_adapter().path, device.path),
                Op::RemoveDevice,
                device.path.addr()
            );
            return;
        }
        remove_all_active_ = false;
    }

    [[nodiscard]] const os::BluezObjectsAdapter& primary_adapter() const noexcept {
        return adapters_[AdapterPlaces{}];
    }

    [[nodiscard]] os::BluezObjectsAdapter* find_adapter(const os::DbusObjectPath& path) noexcept {
        for (auto& adapter : adapters_.prefix(adapter_count_))
            if (adapter.path == path)
                return &adapter;
        return nullptr;
    }

    [[nodiscard]] os::BluezObjectsDevice* find_device(const os::BtDevicePath& path) noexcept {
        for (auto& device : devices_.prefix(device_count_))
            if (device.path == path)
                return &device;
        return nullptr;
    }

    [[nodiscard]] const os::BluezObjectsDevice* find_device(os::BtAddr addr) const noexcept {
        for (const auto& device : devices_.prefix(device_count_))
            if (device.path.addr() == addr)
                return &device;
        return nullptr;
    }

    [[nodiscard]] std::optional<os::BtDevicePath> device_path(os::BtAddr addr) const noexcept {
        const auto* device = find_device(addr);
        if (!device)
            return std::nullopt;
        return device->path;
    }

    [[nodiscard]] os::BtKind device_kind(os::BtAddr addr) const noexcept {
        const auto* device = find_device(addr);
        if (!device)
            return os::BtKind::Other;
        return device->kind.value_or(os::BtKind::Other);
    }

    [[nodiscard]] bool trusted(os::BtAddr addr) const noexcept {
        const auto* device = find_device(addr);
        return device && device->trusted && *device->trusted;
    }

    void set_paired(os::BtAddr addr, bool value) noexcept {
        auto* device = mutable_device(addr);
        if (device)
            device->paired = value;
    }

    void set_trusted(os::BtAddr addr, bool value) noexcept {
        auto* device = mutable_device(addr);
        if (device)
            device->trusted = value;
    }

    void set_connected(os::BtAddr addr, bool value) noexcept {
        auto* device = mutable_device(addr);
        if (device)
            device->connected = value;
    }

    [[nodiscard]] os::BluezObjectsDevice* mutable_device(os::BtAddr addr) noexcept {
        for (auto& device : devices_.prefix(device_count_))
            if (device.path.addr() == addr)
                return &device;
        return nullptr;
    }

    void drop_device(os::BtAddr addr) noexcept {
        for (DevicePlaces at{}; device_count_.covers(at);) {
            if (!(devices_[at].path.addr() == addr)) {
                at = at.next();
                continue;
            }
            devices_[at] = devices_[device_count_.last()];
            device_count_ = device_count_.last();
        }
    }

    void drop_device(const os::BtDevicePath& path) noexcept {
        for (DevicePlaces at{}; device_count_.covers(at); at = at.next()) {
            if (!(devices_[at].path == path))
                continue;
            devices_[at] = devices_[device_count_.last()];
            device_count_ = device_count_.last();
            return;
        }
    }

    void update_presence() noexcept {
        if (!daemon_up_) {
            presence_ = BtPresence::NoDaemon;
            return;
        }
        if (adapter_count_.is_empty()) {
            presence_ = BtPresence::NoAdapter;
            return;
        }
        const bool powered = primary_adapter().powered && *primary_adapter().powered;
        presence_ = powered ? BtPresence::Ready : BtPresence::Off;
    }

    void clear_pair() noexcept {
        pair_addr_.reset();
        pair_started_.reset();
        repair_after_remove_.reset();
        if (activity_.busy()) {
            activity_ = discovery_running_ && pairing_page() ? BtActivity::discovering()
                                                             : BtActivity::idle();
        }
    }

    void maybe_auto_first() noexcept {
        if (!auto_first_ || !pairing_page() || activity_.busy())
            return;
        for (const auto& device : devices_.prefix(device_count_)) {
            if (device.paired && *device.paired)
                continue;
            if (!os::bt_kind_is_input(device.kind.value_or(os::BtKind::Other)))
                continue;
            auto_first_ = false;
            pair_go_ = device.path.addr();
            return;
        }
    }

    void publish() noexcept {
        BtView next;
        next.presence = presence_;
        next.activity = activity_;
        next.failure = failure_;
        if (!same_view(next, view_)) {
            next.gen = view_.gen.next();
            view_ = next;
        }
        publish_devices();
        publish_prompt();
    }

    [[nodiscard]] static bool same_view(const BtView& a, const BtView& b) noexcept {
        return a.presence == b.presence && a.activity == b.activity && a.failure == b.failure;
    }

    [[nodiscard]] bool visible(const os::BluezObjectsDevice& device) const noexcept {
        const bool paired = device.paired && *device.paired;
        switch (page_) {
            case BtPageMode::Paired:
                return paired;
            case BtPageMode::Pairing:
                return os::bt_kind_is_input(device.kind.value_or(os::BtKind::Other));
            case BtPageMode::PairingAll:
                return true;
            case BtPageMode::Closed:
                return false;
        }
        return false;
    }

    void publish_devices() noexcept {
        std::array<std::optional<BtDeviceViewRow>, BtDeviceView::kRows> rows{};
        std::size_t used = 0;
        for (const auto& device : devices_.prefix(device_count_)) {
            if (used >= rows.size())
                break;
            if (!visible(device))
                continue;
            BtDeviceViewRow row;
            row.addr = device.path.addr();
            if (device.alias)
                row.name = *device.alias;
            row.kind = device.kind.value_or(os::BtKind::Other);
            row.paired = device.paired && *device.paired;
            row.trusted = device.trusted && *device.trusted;
            row.connected = device.connected && *device.connected;
            row.rssi = device.rssi;
            rows[used].emplace(row);
            ++used;
        }
        if (rows == device_view_.rows)
            return;
        device_view_.rows = rows;
        device_view_.gen = device_view_.gen.next();
    }

    void publish_prompt() noexcept {
        std::optional<BtPrompt> next;
        if (prompt_)
            next = prompt_->value;
        if (next == prompt_view_.prompt)
            return;
        prompt_view_.prompt = next;
        prompt_view_.gen = prompt_view_.gen.next();
    }

    std::string socket_path_;
    Radio radio_;
    std::optional<Bus> bus_;
    std::array<std::optional<Bus>, kRetiringBuses.size()> retiring_buses_{};

    BtView view_{};
    BtDeviceView device_view_{};
    BtPromptView prompt_view_{};

    BtPresence presence_ = BtPresence::NoBus;
    BtActivity activity_{};
    BtFailure failure_ = BtFailure::None;
    BtPageMode page_ = BtPageMode::Closed;
    bool auto_first_ = false;

    bool started_ = false;
    bool daemon_up_ = false;
    bool agent_registered_ = false;
    bool discovery_running_ = false;

    infra::IndexedArray<os::BluezObjectsAdapter, kAdapterPlaces.size(), AdapterPlaces> adapters_{};
    AdapterPlaces adapter_count_{};
    infra::IndexedArray<os::BluezObjectsDevice, kDevicePlaces.size(), DevicePlaces> devices_{};
    DevicePlaces device_count_{};

    std::array<std::optional<Pending>, kTrackedCalls.size()> pending_{};

    std::optional<os::BtAddr> pair_go_{};
    std::optional<os::BtAddr> connect_go_{};
    std::optional<os::BtAddr> disconnect_go_{};
    std::optional<os::BtAddr> remove_go_{};
    bool cancel_pair_go_ = false;
    bool reset_adapter_go_ = false;
    bool boot_spent_ = false;
    std::optional<os::RawMonotonicClock::time_point> boot_due_{};
    bool remove_all_go_ = false;
    bool remove_all_active_ = false;
    bool discovery_wanted_ = false;
    bool stop_discovery_go_ = false;

    std::optional<os::BtAddr> pair_addr_{};
    std::optional<os::RawMonotonicClock::time_point> pair_started_{};
    std::optional<os::BtAddr> repair_after_remove_{};

    std::optional<PromptState> prompt_{};
    std::optional<os::RawMonotonicClock::time_point> prompt_at_{};
    std::optional<Answer> answer_{};
    PromptGen next_gen_{};
    PinSeed pin_seed_{};

    std::uint32_t open_errors_ = 0;
    std::uint32_t send_errors_ = 0;
    std::uint32_t decode_errors_ = 0;
    std::uint32_t agent_errors_ = 0;
    std::uint32_t retire_refusals_ = 0;
};

}
