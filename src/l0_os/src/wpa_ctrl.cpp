// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/wpa_ctrl.h"

#include <cerrno>
#include <cstring>
#include <span>
#include <string.h>

#include <utility>

#include "infra/byte_view.h"
#include "infra/decimal_text.h"
#include "infra/errno.h"
#include "os/socket_group.h"

namespace mister::os {
namespace {

constexpr std::string_view kClientPrefix{"/tmp/tasty_wpa"};
constexpr std::string_view kGroupName{"wpa"};
constexpr std::string_view kAttach{"ATTACH"};
constexpr std::string_view kDetach{"DETACH"};
constexpr std::string_view kIfnamePrefix{"IFNAME="};

constexpr std::string_view kPing{"PING"};
constexpr std::string_view kStatus{"STATUS"};
constexpr std::string_view kScan{"SCAN"};
constexpr std::string_view kScanResults{"SCAN_RESULTS"};
constexpr std::string_view kListNetworks{"LIST_NETWORKS"};
constexpr std::string_view kAddNetwork{"ADD_NETWORK"};
constexpr std::string_view kSetNetwork{"SET_NETWORK "};
constexpr std::string_view kSsidField{" ssid "};
constexpr std::string_view kPskField{" psk "};
constexpr std::string_view kKeyMgmtField{" key_mgmt "};
constexpr std::string_view kIeee80211wField{" ieee80211w "};
constexpr std::string_view kScanSsidField{" scan_ssid "};
constexpr std::string_view kScanSsidOn{"1"};
constexpr std::string_view kSelectNetwork{"SELECT_NETWORK "};
constexpr std::string_view kEnableNetwork{"ENABLE_NETWORK "};
constexpr std::string_view kRemoveNetwork{"REMOVE_NETWORK "};
constexpr std::string_view kDisconnect{"DISCONNECT"};
constexpr std::string_view kReconnect{"RECONNECT"};
constexpr std::string_view kSetCountry{"SET country "};
constexpr std::string_view kSignalPoll{"SIGNAL_POLL"};

constexpr char kPathSeparator = '/';
constexpr char kEventOpen = '<';
constexpr char kNewline = '\n';
constexpr char kCarriageReturn = '\r';

[[nodiscard]] Error bad_format() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

class CommandBuffer {
public:
    enum : std::size_t { kMaxBytes = 256 };

    void clear() noexcept { used_ = {}; }

    [[nodiscard]] bool append(std::string_view text) noexcept {
        if (text.size() > kMaxBytes - used_.size())
            return false;
        std::memcpy(text_.data() + used_.size(), text.data(), text.size());
        used_ = std::span<char>{text_.data(), used_.size() + text.size()};
        return true;
    }

    [[nodiscard]] bool number(WpaNetworkId id) noexcept { return append(id.text().view()); }

    [[nodiscard]] bool append_ieee80211w(WifiSecurity security) noexcept {
        const infra::DecimalText token{wifi_security_ieee80211w(security)};
        return append(token.view());
    }

    [[nodiscard]] std::size_t size() const noexcept { return used_.size(); }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span<const char>{used_});
    }

    void scrub() noexcept {
        ::explicit_bzero(text_.data(), text_.size());
        used_ = {};
    }

private:
    std::array<char, kMaxBytes> text_{};
    std::span<char> used_{};
};

struct CommandEncoder {
    CommandBuffer& out;
    bool ok = true;

    void on(const WpaCtrlCommand::Ping&) noexcept { ok = ok && out.append(kPing); }

    void on(const WpaCtrlCommand::Status&) noexcept { ok = ok && out.append(kStatus); }

    void on(const WpaCtrlCommand::Scan&) noexcept { ok = ok && out.append(kScan); }

    void on(const WpaCtrlCommand::ScanResults&) noexcept { ok = ok && out.append(kScanResults); }

    void on(const WpaCtrlCommand::ListNetworks&) noexcept { ok = ok && out.append(kListNetworks); }

    void on(const WpaCtrlCommand::AddNetwork&) noexcept { ok = ok && out.append(kAddNetwork); }

    void on(const WpaCtrlCommand::SetSsid& command) noexcept {
        ok = ok
            && out.append(kSetNetwork)
            && out.number(command.id)
            && out.append(kSsidField)
            && out.append(command.ssid.to_hex().view());
    }

    void on(const WpaCtrlCommand::SetPsk& command) noexcept {
        const auto quoted = command.psk.write_quoted();
        ok = ok
            && out.append(kSetNetwork)
            && out.number(command.id)
            && out.append(kPskField)
            && out.append(quoted.view());
    }

    void on(const WpaCtrlCommand::SetKeyMgmt& command) noexcept {
        const std::string_view key_mgmt = wifi_security_key_mgmt(command.security);
        if (key_mgmt.empty()) {
            ok = false;
            return;
        }
        ok = ok
            && out.append(kSetNetwork)
            && out.number(command.id)
            && out.append(kKeyMgmtField)
            && out.append(key_mgmt);
    }

    void on(const WpaCtrlCommand::SetIeee80211w& command) noexcept {
        ok = ok
            && out.append(kSetNetwork)
            && out.number(command.id)
            && out.append(kIeee80211wField)
            && out.append_ieee80211w(command.security);
    }

    void on(const WpaCtrlCommand::SetHidden& command) noexcept {
        ok = ok
            && out.append(kSetNetwork)
            && out.number(command.id)
            && out.append(kScanSsidField)
            && out.append(kScanSsidOn);
    }

    void on(const WpaCtrlCommand::Select& command) noexcept {
        ok = ok && out.append(kSelectNetwork) && out.number(command.id);
    }

    void on(const WpaCtrlCommand::Enable& command) noexcept {
        ok = ok && out.append(kEnableNetwork) && out.number(command.id);
    }

    void on(const WpaCtrlCommand::Remove& command) noexcept {
        ok = ok && out.append(kRemoveNetwork) && out.number(command.id);
    }

    void on(const WpaCtrlCommand::Disconnect&) noexcept { ok = ok && out.append(kDisconnect); }

    void on(const WpaCtrlCommand::Reconnect&) noexcept { ok = ok && out.append(kReconnect); }

    void on(const WpaCtrlCommand::SetCountry& command) noexcept {
        ok = ok && out.append(kSetCountry) && out.append(command.code.code());
    }

    void on(const WpaCtrlCommand::SignalPoll&) noexcept { ok = ok && out.append(kSignalPoll); }

    void misrouted(const WpaCtrlCommand&) noexcept { ok = false; }
};

[[nodiscard]] bool encode(const WpaCtrlCommand& command, CommandBuffer& out) noexcept {
    out.clear();
    CommandEncoder encoder{out};
    infra::dispatch<infra::AllRouted>(command, encoder);
    return encoder.ok && out.size() != 0;
}

struct ScrubSink {
    using Result = WpaCtrlCommand;

    template <class A>
    WpaCtrlCommand on(const A& alternative) const noexcept {
        return infra::make<WpaCtrlCommand>(alternative);
    }

    WpaCtrlCommand on(const WpaCtrlCommand::SetPsk& alternative) const noexcept {
        WpaCtrlCommand::SetPsk clean = alternative;
        clean.psk.scrub();
        return infra::make<WpaCtrlCommand>(clean);
    }

    WpaCtrlCommand misrouted(const WpaCtrlCommand& message) const noexcept { return message; }
};

[[nodiscard]] bool starts_with_event(std::string_view text) noexcept {
    return !text.empty() && text.front() == kEventOpen;
}

[[nodiscard]] bool starts_with_ifname(std::string_view text) noexcept {
    return text.starts_with(kIfnamePrefix);
}

[[nodiscard]] std::string_view without_newline(std::string_view text) noexcept {
    while (!text.empty() && (text.back() == kNewline || text.back() == kCarriageReturn)) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool retryable(const Error& error) noexcept {
    return error.code == Errc::os && error.os_errno() == infra::Errno{EBUSY};
}

}

WpaCtrl::WpaCtrl(UnixDgramSocket command, UnixDgramSocket monitor) noexcept
    : cmd_(std::move(command)),
      monitor_(std::move(monitor)) {}

WpaCtrl::~WpaCtrl() {
    close();
}

Ex<std::optional<WpaCtrl>> WpaCtrl::open(IfName ifname) noexcept {
    return open_in(kControlDir, ifname);
}

Ex<std::optional<WpaCtrl>> WpaCtrl::open_in(std::string_view control_dir, IfName ifname) noexcept {
    std::array<char, UnixDgramSocket::kPathMax> path{};
    const std::size_t need = control_dir.size() + 1 + ifname.size();
    if (control_dir.empty() || need >= path.size())
        return std::unexpected(bad_format());
    std::memcpy(path.data(), control_dir.data(), control_dir.size());
    path[control_dir.size()] = kPathSeparator;
    std::memcpy(path.data() + control_dir.size() + 1, ifname.view().data(), ifname.size());
    path[need] = '\0';
    const std::string_view path_view{path.data(), need};

    auto command = UnixDgramSocket::bind_client(kClientPrefix, SocketGroup::named(kGroupName));
    if (!command)
        return std::unexpected(command.error());
    auto monitor = UnixDgramSocket::bind_client(kClientPrefix, SocketGroup::named(kGroupName));
    if (!monitor)
        return std::unexpected(monitor.error());

    const auto command_connected = command->connect(path_view);
    if (!command_connected) {
        if (command_connected.error().code == Errc::not_found)
            return std::optional<WpaCtrl>{};
        return std::unexpected(command_connected.error());
    }
    const auto monitor_connected = monitor->connect(path_view);
    if (!monitor_connected) {
        if (monitor_connected.error().code == Errc::not_found)
            return std::optional<WpaCtrl>{};
        return std::unexpected(monitor_connected.error());
    }
    const std::string_view attach = kAttach;
    const auto attached = monitor->send(
        std::as_bytes(std::span<const char>{attach.data(), attach.size()})
    );
    if (!attached) {
        if (attached.error().code == Errc::not_found)
            return std::optional<WpaCtrl>{};
        return std::unexpected(attached.error());
    }
    if (!*attached)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    return std::optional<WpaCtrl>{WpaCtrl{std::move(*command), std::move(*monitor)}};
}

void WpaCtrl::reset_head() noexcept {
    sent_ = false;
    sent_at_.reset();
}

void WpaCtrl::pop_head() noexcept {
    (void)queue_.pop();
    reset_head();
}

bool WpaCtrl::send_head_once() noexcept {
    if (!cmd_ || queue_.empty() || sent_)
        return true;
    CommandBuffer text;
    if (!encode(queue_.front(), text)) {
        text.scrub();
        return false;
    }
    const auto sent = cmd_->send(text.bytes());
    text.scrub();
    if (!sent) {
        if (!retryable(sent.error()))
            failed_ = true;
        return false;
    }
    if (!*sent)
        return false;
    sent_ = true;
    scrub_head_secret();
    return true;
}

void WpaCtrl::scrub_head_secret() noexcept {
    if (queue_.empty())
        return;
    ScrubSink sink;
    queue_.front() = infra::dispatch<infra::AllRouted>(queue_.front(), sink);
}

void WpaCtrl::drain_monitor(WpaCtrlPollResult& out) noexcept {

    auto slot = out.events.begin();
    while (slot != out.events.end()) {
        const auto got = monitor_->recv(std::span<std::byte>{rx_.data(), rx_.size()});
        if (!got) {
            failed_ = true;
            return;
        }
        if (!*got)
            return;
        const std::string_view text = infra::as_text(**got);

        if (!starts_with_event(text))
            continue;
        *slot = wpa_event_from_text(text);
        ++slot;
    }
}

bool WpaCtrl::drain_command(RawMonotonicClock::time_point now, WpaCtrlPollResult& out) noexcept {
    if (queue_.empty() || !sent_)
        return false;
    for (;;) {
        const auto got = cmd_->recv(std::span<std::byte>{rx_.data(), rx_.size()});
        if (!got) {
            failed_ = true;
            return false;
        }
        if (!*got)
            return false;
        const std::string_view text = infra::as_text(**got);
        if (starts_with_event(text) || starts_with_ifname(text))
            continue;
        out.reply = WpaCtrlReply{infra::kind_of(queue_.front()), without_newline(text)};
        out.status = WpaCtrlPollResult::Reply;
        pop_head();
        if (!queue_.empty() && send_head_once())
            sent_at_ = now;
        return true;
    }
}

Ex<void> WpaCtrl::submit(WpaCtrlCommand command) noexcept {
    if (!cmd_ || !monitor_)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    if (queue_.full())
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    CommandBuffer probe;
    const bool encodable = encode(command, probe);
    probe.scrub();
    if (!encodable)
        return std::unexpected(bad_format());

    if (queue_.empty())
        reset_head();
    if (!queue_.push(command))
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    (void)send_head_once();
    return {};
}

WpaCtrlPollResult WpaCtrl::poll(RawMonotonicClock::time_point now) noexcept {
    WpaCtrlPollResult out{};
    if (!cmd_ || !monitor_)
        return out;

    drain_monitor(out);

    if (!queue_.empty()) {
        if (!sent_)
            (void)send_head_once();
        if (sent_ && !sent_at_)
            sent_at_ = now;
        if (drain_command(now, out))
            return out;
        if (sent_ && sent_at_ && now - *sent_at_ >= kReplyDeadline) {
            out.status = WpaCtrlPollResult::TimedOut;
            return out;
        }
    }
    if (failed_)
        out.status = WpaCtrlPollResult::TimedOut;
    return out;
}

const UniqueFd* WpaCtrl::cmd_fd() const noexcept {
    return cmd_ ? &cmd_->fd() : nullptr;
}

const UniqueFd* WpaCtrl::mon_fd() const noexcept {
    return monitor_ ? &monitor_->fd() : nullptr;
}

void WpaCtrl::close() noexcept {
    if (!monitor_)
        return;
    const std::string_view detach = kDetach;
    (void)monitor_->send(std::as_bytes(std::span<const char>{detach.data(), detach.size()}));
    monitor_.reset();
    cmd_.reset();
    queue_.clear();
    reset_head();
}

}
