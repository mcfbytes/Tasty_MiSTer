// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/nic_host_port.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <tuple>

#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/hex_text.h"
#include "os/run_sync.h"

namespace mister::svc {

namespace {

constexpr std::chrono::milliseconds kDhcpcdSettle{300};

constexpr std::string_view kProbeName = "nicprobe";

[[nodiscard]] ifreq request_for(std::string_view iface) noexcept {
    ifreq r{};
    const std::size_t n = std::min(iface.size(), std::size_t{IFNAMSIZ - 1});
    std::copy_n(iface.begin(), n, r.ifr_name);
    return r;
}

[[nodiscard]] MacAddress mac_of(const ifreq& r) noexcept {
    return MacAddress::at(
        infra::byte_cast<const std::byte>(
            std::span<const char>(r.ifr_hwaddr.sa_data, MacAddress::kBytes)
        )
    );
}

bool run_cmd(const std::string& cmd) {
    const std::array<const char*, 3> argv{"/bin/sh", "-c", cmd.c_str()};
    const auto rc = os::run_sync(argv);
    return rc && rc->succeeded();
}

void iface_up(std::string_view iface) {
    const infra::UniqueFd s{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
    if (s.get() < 0)
        return;
    ifreq r = request_for(iface);
    if (::ioctl(s.get(), SIOCGIFFLAGS, &r) == 0 && (r.ifr_flags & IFF_UP) == 0) {
        r.ifr_flags |= IFF_UP;
        (void)::ioctl(s.get(), SIOCSIFFLAGS, &r);
    }
}

void offload(std::string_view iface, bool on) {
    const char* const v = on ? "on" : "off";
    std::string cmd = "ethtool -K ";
    cmd += iface;
    for (const char* f : {" gro ", " lro ", " gso ", " tso "}) {
        cmd += f;
        cmd += v;
    }
    cmd += " 2>/dev/null";
    (void)run_cmd(cmd);
}

void set_iface_mac(std::string_view iface, const MacAddress& mac) {
    const infra::UniqueFd s{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
    if (s.get() < 0)
        return;
    ifreq r = request_for(iface);
    if (::ioctl(s.get(), SIOCGIFFLAGS, &r) == 0) {
        r.ifr_flags &= ~IFF_UP;
        (void)::ioctl(s.get(), SIOCSIFFLAGS, &r);
    }
    r = request_for(iface);
    r.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    mac.write(
        infra::byte_cast<std::byte>(std::span<char>(r.ifr_hwaddr.sa_data, MacAddress::kBytes))
    );
    (void)::ioctl(s.get(), SIOCSIFHWADDR, &r);
    r = request_for(iface);
    if (::ioctl(s.get(), SIOCGIFFLAGS, &r) == 0) {
        r.ifr_flags |= IFF_UP;
        (void)::ioctl(s.get(), SIOCSIFFLAGS, &r);
    }
}

[[nodiscard]] std::string mac_text(const MacAddress& m) {
    std::string out;
    for (const std::byte b : m.bytes) {
        if (!out.empty())
            out += ':';
        out += infra::HexText::zero_padded(std::to_integer<std::uint8_t>(b)).c_str();
    }
    return out;
}

}

bool NicHostPort::supports(NicLink link) {
    TASTY_SEAT_BODY(NicHostPort);
    switch (link) {
        case NicLink::Eth1:
            return ::if_nametoindex(std::string{NicLinkPlan::kSecond}.c_str()) != 0;
        case NicLink::Tap:

            if (tap_probe_ == Probe::Unknown) {
                const infra::UniqueFd fd{::open("/dev/net/tun", O_RDWR | O_CLOEXEC)};
                tap_probe_ = fd.get() >= 0 ? Probe::Present : Probe::Absent;
            }
            return tap_probe_ == Probe::Present;
        case NicLink::Macvlan:
            if (macvlan_probe_ == Probe::Unknown) {
                std::string add = "ip link add ";
                add += kProbeName;
                add += " link ";
                add += NicLinkPlan::kOnboard;
                add += " type macvlan mode bridge 2>/dev/null";
                const bool ok = run_cmd(add);
                if (ok) {
                    std::string del = "ip link del ";
                    del += kProbeName;
                    del += " 2>/dev/null";
                    (void)run_cmd(del);
                }
                macvlan_probe_ = ok ? Probe::Present : Probe::Absent;
            }
            return macvlan_probe_ == Probe::Present;
        case NicLink::Off:
        case NicLink::Eth0:
            return true;
        case NicLink::kCount:
            break;
    }
    return false;
}

std::optional<MacAddress> NicHostPort::iface_mac(std::string_view iface) {
    TASTY_SEAT_BODY(NicHostPort);
    const infra::UniqueFd s{::socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ALL))};
    if (s.get() < 0)
        return std::nullopt;
    ifreq r = request_for(iface);
    if (::ioctl(s.get(), SIOCGIFHWADDR, &r) != 0)
        return std::nullopt;
    const MacAddress m = mac_of(r);
    if (m.low_half_zero())
        return std::nullopt;
    return m;
}

void NicHostPort::prepare(const NicLinkPlan& plan, std::optional<MacAddress> carried) {
    TASTY_SEAT_BODY(NicHostPort);
    switch (plan.delivery) {
        case NicLinkPlan::Delivery::Tap:
            return;
        case NicLinkPlan::Delivery::Direct:
            offload(plan.parent, true);
            if (carried)
                set_iface_mac(plan.iface, *carried);
            return;
        case NicLinkPlan::Delivery::Shared:
            offload(plan.parent, false);
            return;
        case NicLinkPlan::Delivery::Macvlan:
            offload(plan.parent, false);
            if (!carried)
                return;
            dhcpcd_deny_(plan.iface);
            if (macvlan_create_(plan, *carried)) {
                created_macvlan_.assign(plan.iface);
            }
            return;
    }
}

MacAddress NicHostPort::host_mac() const noexcept {
    TASTY_SEAT_BODY(NicHostPort);
    return host_mac_;
}

Ex<void> NicHostPort::open(const NicLinkPlan& plan) {
    TASTY_SEAT_BODY(NicHostPort);
    if (plan.delivery == NicLinkPlan::Delivery::Tap)
        return open_tap_(plan.iface);
    return open_packet_(plan.iface, plan.shared());
}

Ex<void> NicHostPort::open_tap_(std::string_view iface) {
    infra::UniqueFd fd{::open("/dev/net/tun", O_RDWR | O_CLOEXEC | O_NONBLOCK)};
    if (fd.get() < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    ifreq r = request_for(iface);
    r.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (::ioctl(fd.get(), TUNSETIFF, &r) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    host_mac_ = MacAddress{};
    if (const infra::UniqueFd s{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)}; s.get() >= 0) {
        ifreq m = request_for(r.ifr_name);
        if (::ioctl(s.get(), SIOCGIFHWADDR, &m) == 0)
            host_mac_ = mac_of(m);
    }
    iface_up(r.ifr_name);
    fd_ = std::move(fd);
    tap_ = true;
    return {};
}

Ex<void> NicHostPort::open_packet_(std::string_view iface, bool promiscuous) {
    tap_ = false;
    infra::UniqueFd fd{
        ::socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(ETH_P_ALL))
    };
    if (fd.get() < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    ifreq r = request_for(iface);
    if (::ioctl(fd.get(), SIOCGIFINDEX, &r) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    const int ifindex = r.ifr_ifindex;
    host_mac_ = MacAddress{};
    if (::ioctl(fd.get(), SIOCGIFHWADDR, &r) == 0)
        host_mac_ = mac_of(r);

    if (::ioctl(fd.get(), SIOCGIFFLAGS, &r) == 0 && (r.ifr_flags & IFF_UP) == 0) {
        r.ifr_flags |= IFF_UP;
        (void)::ioctl(fd.get(), SIOCSIFFLAGS, &r);
    }
    sockaddr_ll addr{};
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = ifindex;
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    if (promiscuous) {
        packet_mreq mreq{};
        mreq.mr_ifindex = ifindex;
        mreq.mr_type = PACKET_MR_PROMISC;
        (void)::setsockopt(fd.get(), SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof mreq);
    }

    constexpr int kReceiveBuffer = 4 * 1024 * 1024;
    const int rcvbuf = kReceiveBuffer;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf, sizeof rcvbuf) < 0) {
        (void)::setsockopt(fd.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
    }
    fd_ = std::move(fd);
    return {};
}

void NicHostPort::filter(std::optional<MacAddress> accept) {
    TASTY_SEAT_BODY(NicHostPort);
    if (fd_.get() < 0 || tap_)
        return;
    if (!accept) {
        const int dummy = 0;
        (void)::setsockopt(fd_.get(), SOL_SOCKET, SO_DETACH_FILTER, &dummy, sizeof dummy);
        return;
    }

    constexpr std::uint32_t kPacketType = static_cast<std::uint32_t>(SKF_AD_OFF + SKF_AD_PKTTYPE);
    constexpr std::uint32_t kDstHead = 0;
    constexpr std::uint32_t kDstTail = sizeof(std::uint32_t);
    constexpr std::uint32_t kAllOnesHead = 0xFFFF'FFFF;
    constexpr std::uint32_t kAllOnesTail = 0xFFFF;
    constexpr std::uint32_t kWholeFrame = 0xFFFF'FFFF;
    constexpr std::uint32_t kNoFrame = 0;
    constexpr std::uint8_t kNext = 0;
    constexpr std::uint8_t kOutgoingToDrop = 8;
    constexpr std::uint8_t kHeadNotOnesToUnicast = 2;
    constexpr std::uint8_t kBroadcastToAccept = 3;
    constexpr std::uint8_t kTailNotOnesToDrop = 4;
    constexpr std::uint8_t kHeadNotOursToDrop = 3;
    constexpr std::uint8_t kTailNotOursToDrop = 1;
    constexpr std::uint16_t kLoadByte = BPF_LD | BPF_B | BPF_ABS;
    constexpr std::uint16_t kLoadWord = BPF_LD | BPF_W | BPF_ABS;
    constexpr std::uint16_t kLoadHalf = BPF_LD | BPF_H | BPF_ABS;
    constexpr std::uint16_t kJumpEq = BPF_JMP | BPF_JEQ | BPF_K;
    constexpr std::uint16_t kReturn = BPF_RET | BPF_K;
    const std::span<const std::byte> m(accept->bytes);
    const std::uint32_t head = infra::load_be<std::uint32_t>(m.first<sizeof(std::uint32_t)>());
    const std::uint32_t tail = infra::load_be<std::uint16_t>(
        m.subspan<sizeof(std::uint32_t), sizeof(std::uint16_t)>()
    );
    std::array code{
        sock_filter{.code = kLoadByte, .jt = kNext, .jf = kNext, .k = kPacketType},
        sock_filter{.code = kJumpEq, .jt = kOutgoingToDrop, .jf = kNext, .k = PACKET_OUTGOING},
        sock_filter{.code = kLoadWord, .jt = kNext, .jf = kNext, .k = kDstHead},
        sock_filter{.code = kJumpEq, .jt = kNext, .jf = kHeadNotOnesToUnicast, .k = kAllOnesHead},
        sock_filter{.code = kLoadHalf, .jt = kNext, .jf = kNext, .k = kDstTail},
        sock_filter{
            .code = kJumpEq,
            .jt = kBroadcastToAccept,
            .jf = kTailNotOnesToDrop,
            .k = kAllOnesTail
        },
        sock_filter{.code = kJumpEq, .jt = kNext, .jf = kHeadNotOursToDrop, .k = head},
        sock_filter{.code = kLoadHalf, .jt = kNext, .jf = kNext, .k = kDstTail},
        sock_filter{.code = kJumpEq, .jt = kNext, .jf = kTailNotOursToDrop, .k = tail},
        sock_filter{.code = kReturn, .jt = kNext, .jf = kNext, .k = kWholeFrame},
        sock_filter{.code = kReturn, .jt = kNext, .jf = kNext, .k = kNoFrame},
    };
    sock_fprog prog{.len = std::tuple_size_v<decltype(code)>, .filter = code.data()};
    (void)::setsockopt(fd_.get(), SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof prog);
}

void NicHostPort::send(std::span<const std::byte> frame) {
    TASTY_SEAT_BODY(NicHostPort);
    if (fd_.get() < 0 || frame.empty())
        return;
    if (tap_) {

        if (::write(fd_.get(), frame.data(), frame.size()) < 0)
            return;
    } else {
        (void)::send(fd_.get(), frame.data(), frame.size(), MSG_DONTWAIT);
    }
}

std::span<std::byte> NicHostPort::receive(std::span<std::byte> into) {
    TASTY_SEAT_BODY(NicHostPort);
    if (fd_.get() < 0)
        return {};
    const ssize_t n = tap_ ? ::read(fd_.get(), into.data(), into.size())
                           : ::recv(fd_.get(), into.data(), into.size(), MSG_DONTWAIT);
    if (n <= 0)
        return {};
    return into.first(static_cast<std::size_t>(n));
}

void NicHostPort::close() noexcept {
    TASTY_SEAT_BODY(NicHostPort);
    fd_.reset();
    tap_ = false;
    if (!created_macvlan_.empty()) {
        std::string del = "ip link del ";
        del += created_macvlan_;
        del += " 2>/dev/null";
        (void)run_cmd(del);
        created_macvlan_.clear();
    }
}

NicHostSeed NicHostPort::host_seed() {
    TASTY_SEAT_BODY(NicHostPort);
    std::array<char, HOST_NAME_MAX + 1> host{};
    (void)::gethostname(host.data(), host.size() - 1);

    const NicHostSeed pid{static_cast<std::uint32_t>(::getpid())};
    const NicHostSeed now{static_cast<std::uint32_t>(::time(nullptr))};
    return NicHostSeed::of_name(std::string_view(host.data())).folded(pid).folded(now);
}

void NicHostPort::dhcpcd_deny_(std::string_view iface) {
    const std::string cfg = "/etc/dhcpcd.conf";
    const std::string name{iface};
    if (!run_cmd("grep '^denyinterfaces' " + cfg + " 2>/dev/null | grep -qw " + name)) {
        (void)run_cmd(
            "if grep -q '^denyinterfaces ' "
            + cfg
            + " 2>/dev/null; then sed -i "
              "'0,/^denyinterfaces /s//denyinterfaces "
            + name
            + " /' "
            + cfg
            + "; else printf 'denyinterfaces "
            + name
            + "\\n' >> "
            + cfg
            + "; fi"
        );
        (void)run_cmd("killall dhcpcd 2>/dev/null; dhcpcd 2>/dev/null &");
        std::this_thread::sleep_for(kDhcpcdSettle);
    }
    (void)run_cmd("ip addr flush dev " + name + " 2>/dev/null");
}

bool NicHostPort::macvlan_create_(const NicLinkPlan& plan, const MacAddress& mac) {
    const std::string child{plan.iface};
    (void)run_cmd("ip link del " + child + " 2>/dev/null");
    if (!run_cmd(
            "ip link add "
            + child
            + " link "
            + std::string{plan.parent}
            + " address "
            + mac_text(mac)
            + " type macvlan mode bridge"
        )) {
        return false;
    }
    (void)run_cmd("ip link set " + child + " up");
    (void)run_cmd("ip addr flush dev " + child);
    return true;
}

}
