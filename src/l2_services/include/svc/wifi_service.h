// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <dirent.h>
#include <sys/stat.h>

#include <array>
#include <charconv>
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
#include "os/child_process.h"
#include "os/freq_mhz.h"
#include "os/if_name.h"
#include "os/raw_monotonic_clock.h"
#include "os/signal_dbm.h"
#include "os/ssid.h"
#include "os/wpa_ctrl.h"
#include "os/wpa_event.h"
#include "os/wpa_network_list.h"
#include "os/wpa_scan_table.h"
#include "os/wpa_status.h"
#include "svc/connect_fd_delta.h"
#include "svc/connect_request.h"
#include "svc/conf_stamp.h"
#include "svc/view_gen.h"
#include "svc/wifi_failure.h"
#include "svc/wifi_link.h"
#include "svc/wifi_presence.h"
#include "svc/wifi_saved_view.h"
#include "svc/wifi_scan_view.h"
#include "svc/wifi_view.h"
#include "svc/wpa_conf_file.h"
#include "svc/wpa_conf_store.h"

namespace mister::svc {

struct LinkRetireDepth {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    friend constexpr bool operator==(LinkRetireDepth, LinkRetireDepth) = default;
};

inline constexpr LinkRetireDepth kRetiringLinks{4};

struct InterfaceSlots {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        const std::size_t places = v;
        return places;
    }

    [[nodiscard]] constexpr bool has_room(InterfaceSlots places) const noexcept {
        return v < places.v;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool within(InterfaceSlots depth) const noexcept { return v < depth.v; }

    [[nodiscard]] constexpr InterfaceSlots prior() const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v - 1);
        return InterfaceSlots{place};
    }

    [[nodiscard]] constexpr InterfaceSlots next() const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v + 1);
        return InterfaceSlots{place};
    }

    friend constexpr bool operator==(InterfaceSlots, InterfaceSlots) = default;
};

inline constexpr InterfaceSlots kInterfaceSlots{8};

template <class L>
concept WpaLink = requires(
    L link,
    const L const_link,
    os::WpaCtrlCommand command,
    os::RawMonotonicClock::time_point now
) {
    { link.submit(command) } -> std::same_as<Ex<void>>;
    { link.poll(now) } -> std::same_as<os::WpaCtrlPollResult>;
    { const_link.cmd_fd() } -> std::same_as<const UniqueFd*>;
    { const_link.mon_fd() } -> std::same_as<const UniqueFd*>;
    { link.close() } -> std::same_as<void>;
};

template <class O>
concept WpaOpener = requires(O opener, os::IfName name) {
    typename O::Link;
    requires WpaLink<typename O::Link>;
    { opener.open(name) } -> std::same_as<Ex<std::optional<typename O::Link>>>;
};

inline constexpr std::string_view kNetClassRoot{"/sys/class/net"};

inline constexpr std::string_view kMinimalConf{
    "ctrl_interface=/run/wpa_supplicant\nupdate_config=1\n\n"
};
inline constexpr std::string_view kWrongKeyReason{"WRONG_KEY"};

inline constexpr std::string_view kRssiKey{"RSSI="};
inline constexpr std::string_view kFrequencyKey{"FREQUENCY="};

template <class Opener, class Store, class Starter>
class BasicWifiService {
    TASTY_SEAT_RESIDENT(Connect);

public:
    using Link = typename Opener::Link;

    static constexpr LinkRetireDepth kRetireDepth = kRetiringLinks;
    static constexpr std::chrono::seconds kScanInterval{10};
    static constexpr std::chrono::seconds kStatusInterval{1};
    static constexpr std::chrono::seconds kJoinDeadline{30};

    BasicWifiService(
        Opener opener,
        Store store,
        Starter starter,
        std::string net_class_root = std::string{kNetClassRoot}
    )
        : opener_(std::move(opener)),
          store_(std::move(store)),
          starter_(std::move(starter)),
          net_class_root_(std::move(net_class_root)) {}

    void on(const ConnectRequest::WifiPage& request) noexcept {
        page_open_ = request.open;
        if (!request.open)
            return;
        status_wanted_ = true;
        scan_wanted_ = true;
    }

    void on(const ConnectRequest::WifiScan&) noexcept {
        scan_wanted_ = true;
        start_wanted_ = true;
    }

    void on(const ConnectRequest::WifiJoin& request) noexcept {
        if (request.security == os::WifiSecurity::Unsupported) {
            failure_ = WifiFailure::Rejected;
            return;
        }
        scrub_join_secret();
        join_ = request;
        join_phase_ = JoinPhase::Idle;
        failure_ = WifiFailure::None;
        start_wanted_ = true;
    }

    void on(const ConnectRequest::WifiForget& request) noexcept {
        forget_ssid_ = request.ssid;
        forget_all_ = false;
    }

    void on(const ConnectRequest::WifiForgetAll&) noexcept {
        forget_all_ = true;
        forget_ssid_.reset();
        forget_index_ = ForgetCursor{};
    }

    void on(const ConnectRequest::WifiSetCountry& request) noexcept {
        country_ = request.code;
        country_wanted_ = true;
    }

    [[nodiscard]] ConnectFdDelta tick(os::RawMonotonicClock::time_point now) noexcept {
        ConnectFdDelta delta;
        reap_child(delta);
        if (presence_ == WifiPresence::NoAdapter)
            probe_presence();
        if (start_wanted_
            && presence_ == WifiPresence::NoSupplicant
            && !link_
            && !child_
            && retry_phase_ == RetryPhase::None) {
            begin_start(delta);
        }
        try_open(delta);
        if (link_)
            service_link(now, delta);
        publish();
        return delta;
    }

    void fds_removed() noexcept {
        for (auto& slot : retiring_links_) {
            if (!slot)
                continue;
            slot->close();
            slot.reset();
        }
    }

    [[nodiscard]] const WifiView& view() const noexcept { return view_; }

    [[nodiscard]] const WifiScanView& scan_view() const noexcept { return scan_view_; }

    [[nodiscard]] const WifiSavedView& saved_view() const noexcept { return saved_view_; }

    [[nodiscard]] bool join_secret_is_scrubbed_for_test() const noexcept {
        return join_secret_scrubbed_;
    }

    static_assert(WpaOpener<Opener>, "the opener must open the link type the link verbs name");
    static_assert(WpaLink<Link>, "the link must carry exactly the open-link verbs");

private:
    enum class JoinPhase : std::uint8_t { Idle, Running, Outcome };
    enum class RetryPhase : std::uint8_t { None, Ifdown, Ifup };
    enum class SetStep : std::uint8_t { KeyMgmt, Ieee, HiddenOrSelect, Select };

    struct Interfaces {
        std::array<os::IfName, kInterfaceSlots.size()> names{};
        InterfaceSlots count{};
    };

    struct ConfAttempts {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool spent(ConfAttempts taken) const noexcept {
            return taken.v >= v;
        }

        [[nodiscard]] constexpr ConfAttempts next() const noexcept {
            const std::uint8_t tries = static_cast<std::uint8_t>(v + 1);
            return ConfAttempts{tries};
        }
    };

    static constexpr ConfAttempts kConfAttempts{2};

    struct NotFoundRun {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr NotFoundRun next() const noexcept {
            const std::uint8_t answers = static_cast<std::uint8_t>(v + 1);
            return NotFoundRun{answers};
        }

        [[nodiscard]] constexpr bool spent(NotFoundRun budget) const noexcept {
            return v >= budget.v;
        }

        friend constexpr bool operator==(NotFoundRun, NotFoundRun) = default;
    };

    static constexpr NotFoundRun kNotFoundBudget{2};

    struct ForgetCursor {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr bool within_rows() const noexcept {
            const std::size_t rows = WifiSavedView::kRows;
            return v < rows;
        }

        [[nodiscard]] constexpr ForgetCursor next() const noexcept {
            const std::uint8_t place = static_cast<std::uint8_t>(v + 1);
            return ForgetCursor{place};
        }

        friend constexpr bool operator==(ForgetCursor, ForgetCursor) = default;
    };

    struct SignalReading {
        std::optional<os::SignalDbm> signal{};
        std::optional<os::FreqMhz> freq{};
    };

    [[nodiscard]] static bool preferred(std::string_view name) noexcept {
        return name == std::string_view{"wlan0"};
    }

    [[nodiscard]] static bool before(const os::IfName& a, const os::IfName& b) noexcept {
        if (preferred(a.view()) != preferred(b.view()))
            return preferred(a.view());
        return a.view() < b.view();
    }

    [[nodiscard]] Interfaces probe_interfaces() const noexcept {
        Interfaces out;
        ::DIR* dir = ::opendir(net_class_root_.c_str());
        if (dir == nullptr)
            return out;
        while (const ::dirent* entry = ::readdir(dir)) {
            const std::string_view name{entry->d_name};
            if (name.empty() || name.front() == '.')
                continue;
            const auto ifname = os::IfName::parse(name);
            if (!ifname)
                continue;
            const std::string wireless = net_class_root_ + "/" + std::string{name} + "/wireless";
            struct stat st{};
            if (::stat(wireless.c_str(), &st) != 0)
                continue;
            if (!out.count.has_room(kInterfaceSlots))
                break;
            infra::at_index(out.names, out.count) = *ifname;
            out.count = out.count.next();
        }
        (void)::closedir(dir);
        for (InterfaceSlots i{1}; i.within(out.count); i = i.next()) {
            const os::IfName key = infra::at_index(out.names, i);
            InterfaceSlots j = i;
            while (!j.is_empty() && before(key, infra::at_index(out.names, j.prior()))) {
                infra::at_index(out.names, j) = infra::at_index(out.names, j.prior());
                j = j.prior();
            }
            infra::at_index(out.names, j) = key;
        }
        return out;
    }

    void probe_presence() noexcept {
        if (!probe_interfaces().count.is_empty())
            presence_ = WifiPresence::NoSupplicant;
    }

    void begin_start(ConnectFdDelta& delta) noexcept {
        const Interfaces interfaces = probe_interfaces();
        if (interfaces.count.is_empty()) {
            presence_ = WifiPresence::NoAdapter;
            start_wanted_ = false;
            return;
        }
        ifname_ = interfaces.names[0];
        if (!ensure_conf()) {
            start_wanted_ = false;
            return;
        }
        start_wanted_ = false;
        auto child = starter_.start(*ifname_);
        if (!child) {
            failure_ = WifiFailure::SupplicantWontStart;
            return;
        }
        child_ = std::move(*child);
        const ConnectFdDelta::Fd pidfd{child_->pidfd()};
        if (pidfd.open())
            delta.add(pidfd);
        retry_phase_ = RetryPhase::None;
    }

    void reap_child(ConnectFdDelta& delta) noexcept {
        if (!child_)
            return;
        const ConnectFdDelta::Fd pidfd{child_->pidfd()};
        const auto exit = child_->try_reap();
        if (!exit)
            return;
        child_.reset();
        if (pidfd.open())
            delta.remove(pidfd);
        handle_child_exit(*exit, delta);
    }

    void handle_child_exit(const os::ChildProcessExit& exit, ConnectFdDelta& delta) noexcept {
        const bool ok = exit.succeeded();
        if (link_) {
            retry_phase_ = RetryPhase::None;
            return;
        }
        if (retry_phase_ == RetryPhase::None) {
            if (ok)
                return;
            start_retry(RetryPhase::Ifdown, delta);
            return;
        }
        if (retry_phase_ == RetryPhase::Ifdown) {
            start_retry(RetryPhase::Ifup, delta);
            return;
        }
        retry_phase_ = RetryPhase::None;
        if (!ok)
            failure_ = WifiFailure::SupplicantWontStart;
    }

    void start_retry(RetryPhase phase, ConnectFdDelta& delta) noexcept {
        if (!ifname_)
            return;
        auto child = phase == RetryPhase::Ifdown ? starter_.stop(*ifname_)
                                                 : starter_.start(*ifname_);
        if (!child) {
            retry_phase_ = RetryPhase::None;
            failure_ = WifiFailure::SupplicantWontStart;
            return;
        }
        child_ = std::move(*child);
        retry_phase_ = phase;
        const ConnectFdDelta::Fd pidfd{child_->pidfd()};
        if (pidfd.open())
            delta.add(pidfd);
    }

    [[nodiscard]] static ConnectFdDelta::Fd delta_fd_(const UniqueFd* fd) noexcept {
        return fd ? ConnectFdDelta::Fd{fd->get()} : ConnectFdDelta::Fd{};
    }

    void try_open(ConnectFdDelta& delta) noexcept {
        if (link_ || presence_ == WifiPresence::NoAdapter)
            return;
        const Interfaces interfaces = probe_interfaces();
        if (interfaces.count.is_empty()) {
            presence_ = WifiPresence::NoAdapter;
            return;
        }
        for (InterfaceSlots i{}; i.within(interfaces.count); i = i.next()) {
            auto opened = opener_.open(infra::at_index(interfaces.names, i));
            if (!opened) {
                failure_ = WifiFailure::SupplicantWontStart;
                return;
            }
            if (!opened->has_value())
                continue;
            std::optional<Link> got = std::move(*opened);
            link_.emplace(std::move(*got));
            ifname_ = infra::at_index(interfaces.names, i);
            const ConnectFdDelta::Fd cmd_fd = delta_fd_(link_->cmd_fd());
            const ConnectFdDelta::Fd mon_fd = delta_fd_(link_->mon_fd());
            if (cmd_fd.open())
                delta.add(cmd_fd);
            if (mon_fd.open())
                delta.add(mon_fd);
            presence_ = WifiPresence::Ready;
            failure_ = WifiFailure::None;
            status_wanted_ = true;
            return;
        }
    }

    void retire_link(ConnectFdDelta& delta) noexcept {
        if (!link_)
            return;
        for (auto& slot : retiring_links_) {
            if (slot)
                continue;
            const ConnectFdDelta::Fd cmd_fd = delta_fd_(link_->cmd_fd());
            const ConnectFdDelta::Fd mon_fd = delta_fd_(link_->mon_fd());
            if (cmd_fd.open())
                delta.remove(cmd_fd);
            if (mon_fd.open())
                delta.remove(mon_fd);
            slot.emplace(std::move(*link_));
            link_.reset();
            await_.reset();
            presence_ = WifiPresence::NoSupplicant;
            link_state_ = WifiLink::Disconnected;
            current_ssid_.reset();
            ipv4_.reset();
            signal_.reset();
            freq_.reset();
            last_scan_at_.reset();
            last_status_at_.reset();
            return;
        }
    }

    [[nodiscard]] static bool due(
        const std::optional<os::RawMonotonicClock::time_point>& last,
        os::RawMonotonicClock::time_point now,
        std::chrono::seconds interval
    ) noexcept {
        return !last || now - *last >= interval;
    }

    void service_link(os::RawMonotonicClock::time_point now, ConnectFdDelta& delta) noexcept {
        const os::WpaCtrlPollResult result = link_->poll(now);
        for (const os::WpaEvent& event : result.drained()) {
            handle_event(event, delta);
        }
        if (!link_)
            return;
        if (result.status == os::WpaCtrlPollResult::Reply && result.reply) {
            await_.reset();
            handle_reply(*result.reply, now);
        } else if (result.status == os::WpaCtrlPollResult::TimedOut) {
            retire_link(delta);
            return;
        }
        if (!link_)
            return;

        schedule_country();
        if (!link_ || await_)
            return;
        schedule_cleanup();
        if (!link_ || await_)
            return;
        schedule_forget();
        if (!link_ || await_)
            return;

        if (join_ && join_phase_ == JoinPhase::Idle)
            begin_join();
        if (!link_ || await_)
            return;
        if (join_phase_ == JoinPhase::Outcome
            && join_started_
            && now - *join_started_ >= kJoinDeadline) {
            fail_join(WifiFailure::NotFound);
        }
        if (!link_ || await_)
            return;

        if (scan_results_wanted_) {
            if (submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::ScanResults{}),
                    os::WpaCtrlCommand::Kind::ScanResults
                )) {
                scan_results_wanted_ = false;
            }
            return;
        }
        if (saved_wanted_) {
            if (submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::ListNetworks{}),
                    os::WpaCtrlCommand::Kind::ListNetworks
                )) {
                saved_wanted_ = false;
            }
            return;
        }
        if (scan_wanted_ || (page_open_ && due(last_scan_at_, now, kScanInterval))) {
            if (submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::Scan{}),
                    os::WpaCtrlCommand::Kind::Scan
                )) {
                scan_wanted_ = false;
                last_scan_at_ = now;
            }
            return;
        }
        if (signal_wanted_) {
            if (submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::SignalPoll{}),
                    os::WpaCtrlCommand::Kind::SignalPoll
                )) {
                signal_wanted_ = false;
            }
            return;
        }
        if (status_wanted_ || (page_open_ && due(last_status_at_, now, kStatusInterval))) {
            if (submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::Status{}),
                    os::WpaCtrlCommand::Kind::Status
                )) {
                status_wanted_ = false;
                last_status_at_ = now;
            }
        }
    }

    [[nodiscard]] bool submit(
        const os::WpaCtrlCommand& command,
        os::WpaCtrlCommand::Kind kind
    ) noexcept {
        if (!link_ || await_)
            return false;
        const auto sent = link_->submit(command);
        if (!sent)
            return false;
        await_ = kind;
        if (kind == os::WpaCtrlCommand::Kind::SetPsk && join_) {
            join_->pass.scrub();
            join_secret_scrubbed_ = true;
        }
        return true;
    }

    void handle_event(const os::WpaEvent& event, ConnectFdDelta& delta) noexcept {
        switch (event.kind) {
            case os::WpaEvent::Kind::ScanResults:
                scan_results_wanted_ = true;
                break;
            case os::WpaEvent::Kind::ScanFailed:
                scan_wanted_ = true;
                break;
            case os::WpaEvent::Kind::Connected:
                link_state_ = WifiLink::Connected;
                status_wanted_ = true;
                signal_wanted_ = true;
                if (join_ && join_phase_ == JoinPhase::Outcome)
                    finish_join();
                break;
            case os::WpaEvent::Kind::Disconnected:
                link_state_ = WifiLink::Disconnected;
                status_wanted_ = true;
                break;
            case os::WpaEvent::Kind::SsidTempDisabled: {
                const auto reason = infra::as<os::WpaEvent::SsidTempDisabled>(event);
                if (reason && join_ && reason->reason.view() == kWrongKeyReason) {
                    fail_join(WifiFailure::WrongKey);
                }
                break;
            }
            case os::WpaEvent::Kind::NetworkNotFound:
                if (join_) {
                    not_found_run_ = not_found_run_.next();
                    if (not_found_run_.spent(kNotFoundBudget))
                        fail_join(WifiFailure::NotFound);
                }
                break;
            case os::WpaEvent::Kind::AssocReject:
                if (join_)
                    fail_join(WifiFailure::Rejected);
                break;
            case os::WpaEvent::Kind::Terminating:
                cancel_join();
                retire_link(delta);
                break;
            case os::WpaEvent::Kind::RegdomChange:
                status_wanted_ = true;
                break;
            case os::WpaEvent::Kind::Unhandled:
            case os::WpaEvent::Kind::kCount:
                break;
        }
    }

    void handle_reply(
        const os::WpaCtrlReply& reply,
        os::RawMonotonicClock::time_point now
    ) noexcept {
        switch (reply.kind) {
            case os::WpaCtrlCommand::Kind::Status: {
                const std::string text = terminated(reply.text);
                const auto status = os::WpaStatus::parse(text);
                if (status)
                    apply_status(*status);
                if (page_open_)
                    signal_wanted_ = true;
                break;
            }
            case os::WpaCtrlCommand::Kind::SignalPoll: {
                const SignalReading reading = read_signal(reply.text);
                if (reading.signal)
                    signal_ = reading.signal;
                if (reading.freq)
                    freq_ = reading.freq;
                break;
            }
            case os::WpaCtrlCommand::Kind::ScanResults: {
                const std::string text = terminated(reply.text);
                const auto table = os::WpaScanTable::parse(text);
                if (table) {
                    apply_scan(*table);
                    saved_wanted_ = true;
                }
                break;
            }
            case os::WpaCtrlCommand::Kind::ListNetworks: {
                const std::string text = terminated(reply.text);
                const auto list = os::WpaNetworkList::parse(text);
                if (list)
                    apply_saved(*list);
                break;
            }
            case os::WpaCtrlCommand::Kind::AddNetwork:
                handle_add_network(reply.text);
                break;
            case os::WpaCtrlCommand::Kind::SetSsid:
                handle_set_ssid(reply.text);
                break;
            case os::WpaCtrlCommand::Kind::SetPsk:
                handle_set_generic(reply.text, SetStep::KeyMgmt);
                break;
            case os::WpaCtrlCommand::Kind::SetKeyMgmt:
                handle_set_generic(reply.text, SetStep::Ieee);
                break;
            case os::WpaCtrlCommand::Kind::SetIeee80211w:
                handle_set_generic(reply.text, SetStep::HiddenOrSelect);
                break;
            case os::WpaCtrlCommand::Kind::SetHidden:
                handle_set_generic(reply.text, SetStep::Select);
                break;
            case os::WpaCtrlCommand::Kind::Select:
                handle_select(reply.text, now);
                break;
            case os::WpaCtrlCommand::Kind::Remove:
                handle_remove(reply.text);
                break;

            case os::WpaCtrlCommand::Kind::Ping:
            case os::WpaCtrlCommand::Kind::Scan:
            case os::WpaCtrlCommand::Kind::Enable:
            case os::WpaCtrlCommand::Kind::Disconnect:
            case os::WpaCtrlCommand::Kind::Reconnect:
            case os::WpaCtrlCommand::Kind::SetCountry:
            case os::WpaCtrlCommand::Kind::kCount:
                break;
        }
    }

    void handle_add_network(std::string_view text) noexcept {
        if (!join_)
            return;
        const auto id = os::WpaNetworkId::parse(text);
        if (!id) {
            fail_join(WifiFailure::NotFound);
            return;
        }
        join_id_ = *id;
        (void)submit(
            infra::make<os::WpaCtrlCommand>(
                os::WpaCtrlCommand::SetSsid{.ssid = join_->ssid, .id = *join_id_}
            ),
            os::WpaCtrlCommand::Kind::SetSsid
        );
    }

    void handle_set_ssid(std::string_view text) noexcept {
        if (!join_ || !join_id_ || !reply_ok(text)) {
            if (join_)
                fail_join(WifiFailure::Rejected);
            return;
        }
        if (join_->security == os::WifiSecurity::Open) {
            submit_key_mgmt();
            return;
        }
        (void)submit(
            infra::make<os::WpaCtrlCommand>(
                os::WpaCtrlCommand::SetPsk{.psk = join_->pass, .id = *join_id_}
            ),
            os::WpaCtrlCommand::Kind::SetPsk
        );
    }

    void handle_set_generic(std::string_view text, SetStep step) noexcept {
        if (!join_ || !join_id_)
            return;
        if (!reply_ok(text)) {
            fail_join(WifiFailure::Rejected);
            return;
        }
        switch (step) {
            case SetStep::KeyMgmt:
                submit_key_mgmt();
                return;
            case SetStep::Ieee:
                (void)submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::SetIeee80211w{
                        .security = join_->security,
                        .id = *join_id_
                    }),
                    os::WpaCtrlCommand::Kind::SetIeee80211w
                );
                return;
            case SetStep::HiddenOrSelect:
                if (!join_->hidden) {
                    submit_select();
                    return;
                }
                (void)submit(
                    infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::SetHidden{.id = *join_id_}),
                    os::WpaCtrlCommand::Kind::SetHidden
                );
                return;
            case SetStep::Select:
                submit_select();
                return;
        }
    }

    void submit_key_mgmt() noexcept {
        if (!join_ || !join_id_)
            return;
        (void)submit(
            infra::make<os::WpaCtrlCommand>(
                os::WpaCtrlCommand::SetKeyMgmt{.security = join_->security, .id = *join_id_}
            ),
            os::WpaCtrlCommand::Kind::SetKeyMgmt
        );
    }

    void submit_select() noexcept {
        if (!join_id_)
            return;
        (void)submit(
            infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::Select{.id = *join_id_}),
            os::WpaCtrlCommand::Kind::Select
        );
    }

    void handle_select(std::string_view text, os::RawMonotonicClock::time_point now) noexcept {
        if (!join_)
            return;
        if (!reply_ok(text)) {
            fail_join(WifiFailure::Rejected);
            return;
        }
        join_phase_ = JoinPhase::Outcome;
        join_started_ = now;
    }

    void handle_remove(std::string_view text) noexcept {
        (void)text;
        if (forget_all_)
            return;
        if (!forget_ssid_)
            return;
        rewrite_conf_forget(*forget_ssid_);
        forget_ssid_.reset();
    }

    [[nodiscard]] static bool reply_ok(std::string_view text) noexcept {
        return text == std::string_view{"OK"};
    }

    [[nodiscard]] static std::string terminated(std::string_view text) {
        std::string out{text};
        if (out.empty() || out.back() != '\n')
            out.push_back('\n');
        return out;
    }

    void begin_join() noexcept {
        if (!join_ || !link_)
            return;
        staged_pass_ = join_->pass;
        join_secret_scrubbed_ = false;
        not_found_run_ = NotFoundRun{};
        join_phase_ = JoinPhase::Running;
        if (!submit(
                infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::AddNetwork{}),
                os::WpaCtrlCommand::Kind::AddNetwork
            )) {
            join_phase_ = JoinPhase::Idle;
        }
    }

    void finish_join() noexcept {
        if (!join_)
            return;
        if (persist_join())
            failure_ = WifiFailure::None;
        current_ssid_ = join_->ssid;
        link_state_ = WifiLink::Connected;
        scrub_join_secret();
        join_.reset();
        join_phase_ = JoinPhase::Idle;
        join_id_.reset();
        join_started_.reset();
        status_wanted_ = true;
        signal_wanted_ = true;
    }

    void fail_join(WifiFailure failure) noexcept {
        failure_ = failure;
        if (join_id_)
            cleanup_id_ = join_id_;
        scrub_join_secret();
        join_.reset();
        join_phase_ = JoinPhase::Idle;
        join_id_.reset();
        join_started_.reset();
    }

    void cancel_join() noexcept {
        scrub_join_secret();
        join_.reset();
        join_phase_ = JoinPhase::Idle;
        join_id_.reset();
        join_started_.reset();
    }

    void scrub_join_secret() noexcept {
        if (join_)
            join_->pass.scrub();
        staged_pass_.scrub();
    }

    [[nodiscard]] bool persist_join() noexcept {
        if (!join_)
            return false;
        for (ConfAttempts attempt{}; !kConfAttempts.spent(attempt); attempt = attempt.next()) {
            auto conf = load_conf();
            if (!conf)
                return false;
            const os::Passphrase* pass = join_->security == os::WifiSecurity::Open ? nullptr
                                                                                   : &staged_pass_;
            if (!conf->first.add(join_->ssid, join_->security, pass, join_->hidden)) {
                failure_ = WifiFailure::ConfUnreadable;
                return false;
            }
            if (save_conf_once(conf->first, conf->second))
                return true;
        }
        failure_ = WifiFailure::ConfUnreadable;
        return false;
    }

    void schedule_cleanup() noexcept {
        if (!cleanup_id_ || !link_ || await_)
            return;
        if (submit(
                infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::Remove{.id = *cleanup_id_}),
                os::WpaCtrlCommand::Kind::Remove
            )) {
            cleanup_id_.reset();
        }
    }

    void schedule_forget() noexcept {
        if ((!forget_all_ && !forget_ssid_) || !link_ || await_)
            return;
        if (forget_all_) {
            for (ForgetCursor at = forget_index_; at.within_rows(); at = at.next()) {
                if (!infra::at_index(saved_view_.rows, at))
                    break;
                forget_index_ = at.next();
                (void)submit(
                    infra::make<os::WpaCtrlCommand>(
                        os::WpaCtrlCommand::Remove{.id = infra::at_index(saved_view_.rows, at)->id}
                    ),
                    os::WpaCtrlCommand::Kind::Remove
                );
                return;
            }
            rewrite_conf_forget_all();
            forget_all_ = false;
            forget_index_ = ForgetCursor{};
            return;
        }
        for (auto& slot : saved_view_.rows) {
            if (!slot)
                break;
            if (slot->ssid != *forget_ssid_)
                continue;
            (void)submit(
                infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::Remove{.id = slot->id}),
                os::WpaCtrlCommand::Kind::Remove
            );
            return;
        }
        rewrite_conf_forget(*forget_ssid_);
        forget_ssid_.reset();
    }

    void schedule_country() noexcept {
        if (!country_wanted_ || !country_)
            return;
        country_wanted_ = false;
        rewrite_conf_country(*country_);
        if (!link_ || await_)
            return;
        (void)submit(
            infra::make<os::WpaCtrlCommand>(os::WpaCtrlCommand::SetCountry{.code = *country_}),
            os::WpaCtrlCommand::Kind::SetCountry
        );
    }

    [[nodiscard]] std::optional<std::pair<WpaConfFile, ConfStamp>> load_conf() noexcept {
        auto loaded = store_.load();
        if (loaded)
            return std::make_pair(std::move(loaded->file()), loaded->stamp());
        if (loaded.error().code != Errc::not_found) {
            failure_ = WifiFailure::ConfUnreadable;
            return std::nullopt;
        }
        auto parsed = WpaConfFile::parse(kMinimalConf);
        if (!parsed) {
            failure_ = WifiFailure::ConfUnreadable;
            return std::nullopt;
        }
        if (country_)
            parsed->set_country(*country_);
        return std::make_pair(std::move(*parsed), ConfStamp::absent());
    }

    [[nodiscard]] bool save_conf_once(WpaConfFile& file, ConfStamp stamp) noexcept {
        const auto saved = store_.save(file, stamp);
        if (saved && *saved == WpaConfStoreSaveResult::Saved)
            return true;
        if (!saved)
            failure_ = WifiFailure::ConfUnreadable;
        return false;
    }

    [[nodiscard]] bool ensure_conf() noexcept {
        for (ConfAttempts attempt{}; !kConfAttempts.spent(attempt); attempt = attempt.next()) {
            auto conf = load_conf();
            if (!conf)
                return false;
            if (conf->second.present())
                return true;
            if (save_conf_once(conf->first, conf->second))
                return true;
        }
        failure_ = WifiFailure::ConfUnreadable;
        return false;
    }

    void rewrite_conf_forget(const os::Ssid& ssid) noexcept {
        for (ConfAttempts attempt{}; !kConfAttempts.spent(attempt); attempt = attempt.next()) {
            auto conf = load_conf();
            if (!conf)
                return;
            conf->first.remove(ssid);
            if (save_conf_once(conf->first, conf->second))
                return;
        }
        failure_ = WifiFailure::ConfUnreadable;
    }

    void rewrite_conf_forget_all() noexcept {
        for (ConfAttempts attempt{}; !kConfAttempts.spent(attempt); attempt = attempt.next()) {
            auto conf = load_conf();
            if (!conf)
                return;
            for (auto& slot : saved_view_.rows) {
                if (!slot)
                    break;
                conf->first.remove(slot->ssid);
            }
            if (save_conf_once(conf->first, conf->second))
                return;
        }
        failure_ = WifiFailure::ConfUnreadable;
    }

    void rewrite_conf_country(os::CountryCode code) noexcept {
        for (ConfAttempts attempt{}; !kConfAttempts.spent(attempt); attempt = attempt.next()) {
            auto conf = load_conf();
            if (!conf)
                return;
            conf->first.set_country(code);
            if (save_conf_once(conf->first, conf->second))
                return;
        }
        failure_ = WifiFailure::ConfUnreadable;
    }

    [[nodiscard]] static SignalReading read_signal(std::string_view text) noexcept {
        SignalReading out;
        std::string_view rest = text;
        while (!rest.empty()) {
            const std::size_t newline = rest.find('\n');
            std::string_view line = newline == std::string_view::npos ? rest
                                                                      : rest.substr(0, newline);
            rest = newline == std::string_view::npos ? std::string_view{}
                                                     : rest.substr(newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line.starts_with(kRssiKey)) {
                std::int16_t value = 0;
                const std::string_view digits = line.substr(kRssiKey.size());
                const auto parsed = std::from_chars(
                    digits.data(),
                    digits.data() + digits.size(),
                    value
                );
                if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size()) {
                    out.signal = os::SignalDbm::of(value);
                }
            } else if (line.starts_with(kFrequencyKey)) {
                std::uint16_t value = 0;
                const std::string_view digits = line.substr(kFrequencyKey.size());
                const auto parsed = std::from_chars(
                    digits.data(),
                    digits.data() + digits.size(),
                    value
                );
                if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size()) {
                    out.freq = os::FreqMhz::of(value);
                }
            }
        }
        return out;
    }

    void apply_status(const os::WpaStatus& status) noexcept {
        link_state_ = wifi_link_from_state(status.state());
        if (status.ssid())
            current_ssid_ = *status.ssid();
        ipv4_ = status.ip();
        if (status.freq())
            freq_ = *status.freq();
        bssid_ = status.bssid();
        wpa_state_ = status.state();
    }

    [[nodiscard]] bool is_saved(const os::Ssid& ssid) const noexcept {
        for (const auto& slot : saved_view_.rows) {
            if (!slot)
                break;
            if (slot->ssid == ssid)
                return true;
        }
        return false;
    }

    void apply_scan(const os::WpaScanTable& table) noexcept {
        WifiScanView next;
        next.gen = scan_view_.gen.next();
        std::size_t used = 0;
        for (const os::WpaScanTableRow& row : table.rows()) {
            if (row.ssid.empty())
                continue;
            std::size_t at = used;
            bool found = false;
            for (std::size_t i = 0; i < used; ++i) {
                if (next.rows[i] && next.rows[i]->ssid == row.ssid) {
                    at = i;
                    found = true;
                    break;
                }
            }
            if (found) {
                auto& kept = *next.rows[at];
                if (row.signal.dbm() > kept.signal.dbm()) {
                    kept.signal = row.signal;
                    kept.freq = row.freq;
                }
                continue;
            }
            if (used >= WifiScanView::kRows)
                break;
            WifiScanViewRow entry;
            entry.ssid = row.ssid;
            entry.signal = row.signal;
            entry.freq = row.freq;
            entry.security = row.security;
            entry.saved = is_saved(row.ssid);
            next.rows[used].emplace(entry);
            ++used;
        }
        scan_view_ = next;
    }

    void apply_saved(const os::WpaNetworkList& list) noexcept {
        WifiSavedView next;
        next.gen = saved_view_.gen.next();
        std::size_t used = 0;
        for (const os::WpaNetworkListRow& row : list.rows()) {
            if (used >= WifiSavedView::kRows)
                break;
            WifiSavedViewRow entry;
            entry.id = row.id;
            entry.ssid = row.ssid;
            entry.current = row.current;
            entry.disabled = row.disabled;
            next.rows[used].emplace(entry);
            ++used;
        }
        saved_view_ = next;
        bool changed = false;
        for (auto& slot : scan_view_.rows) {
            if (!slot)
                break;
            const bool saved = is_saved(slot->ssid);
            if (slot->saved == saved)
                continue;
            slot->saved = saved;
            changed = true;
        }
        if (changed)
            scan_view_.gen = scan_view_.gen.next();
    }

    void publish() noexcept {
        WifiView next;
        next.presence = presence_;
        next.link = link_state_;
        next.failure = failure_;
        next.ssid = current_ssid_;
        next.ipv4 = ipv4_;
        next.signal = signal_;
        next.freq = freq_;
        next.ifname = ifname_;
        next.bssid = bssid_;
        next.wpa_state = wpa_state_;
        next.country = country_;
        if (same_content(next, view_))
            return;
        next.gen = view_.gen.next();
        view_ = next;
    }

    [[nodiscard]] static bool same_content(const WifiView& a, const WifiView& b) noexcept {
        return a.presence == b.presence
            && a.link == b.link
            && a.failure == b.failure
            && a.ssid == b.ssid
            && a.ipv4 == b.ipv4
            && a.signal == b.signal
            && a.freq == b.freq
            && a.ifname == b.ifname
            && a.bssid == b.bssid
            && a.wpa_state == b.wpa_state
            && a.country == b.country;
    }

    Opener opener_;
    Store store_;
    Starter starter_;
    std::string net_class_root_;

    WifiView view_{};
    WifiScanView scan_view_{};
    WifiSavedView saved_view_{};

    WifiPresence presence_ = WifiPresence::NoAdapter;
    WifiFailure failure_ = WifiFailure::None;
    WifiLink link_state_ = WifiLink::Disconnected;
    std::optional<os::IfName> ifname_{};
    std::optional<Link> link_{};
    std::optional<os::ChildProcess> child_{};
    std::array<std::optional<Link>, kRetiringLinks.size()> retiring_links_{};
    RetryPhase retry_phase_ = RetryPhase::None;

    std::optional<ConnectRequest::WifiJoin> join_{};
    os::Passphrase staged_pass_{};
    JoinPhase join_phase_ = JoinPhase::Idle;
    std::optional<os::WpaNetworkId> join_id_{};
    std::optional<os::WpaNetworkId> cleanup_id_{};
    std::optional<os::RawMonotonicClock::time_point> join_started_{};
    NotFoundRun not_found_run_{};
    bool join_secret_scrubbed_ = false;

    std::optional<os::WpaCtrlCommand::Kind> await_{};
    bool start_wanted_ = false;
    bool scan_wanted_ = false;
    bool status_wanted_ = false;
    bool signal_wanted_ = false;
    bool scan_results_wanted_ = false;
    bool saved_wanted_ = false;
    bool page_open_ = false;
    std::optional<os::RawMonotonicClock::time_point> last_scan_at_{};
    std::optional<os::RawMonotonicClock::time_point> last_status_at_{};

    std::optional<os::CountryCode> country_{};
    bool country_wanted_ = false;
    std::optional<os::Ssid> current_ssid_{};
    std::optional<os::Ipv4Addr> ipv4_{};
    std::optional<os::SignalDbm> signal_{};
    std::optional<os::FreqMhz> freq_{};
    std::optional<os::Bssid> bssid_{};
    os::WpaState wpa_state_ = os::WpaState::Unknown;

    std::optional<os::Ssid> forget_ssid_{};
    bool forget_all_ = false;
    ForgetCursor forget_index_{};
};

}
