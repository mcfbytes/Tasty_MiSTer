// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/a2065_mac_munge.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/span_cut.h"

namespace mister::cores::minimig {

namespace {

using infra::ByteCount;
using infra::ByteOffset;
using infra::affine::first_clamped;
using infra::affine::from;
using infra::affine::operator+;
using infra::affine::operator+=;

struct NetWord {
    std::uint16_t v = 0;

    [[nodiscard]] static NetWord at(std::span<const std::byte> f, ByteOffset at) noexcept {
        return NetWord{infra::load_be<std::uint16_t>(from(f, at).first<sizeof(std::uint16_t)>())};
    }

    [[nodiscard]] static NetWord padded_at(std::span<const std::byte> f, ByteOffset at) noexcept {
        std::array<std::byte, sizeof(std::uint16_t)> b{};
        std::ranges::copy(first_clamped(from(f, at), ByteCount{b.size()}), b.begin());
        return NetWord{
            infra::load_be<std::uint16_t>(std::span<const std::byte, sizeof(std::uint16_t)>(b))
        };
    }

    [[nodiscard]] static constexpr NetWord of_byte(std::byte b) noexcept {
        return NetWord{std::to_integer<std::uint16_t>(b)};
    }

    void put(std::span<std::byte> f, ByteOffset at) const noexcept {
        infra::store_be<std::uint16_t>(from(f, at).first<sizeof(std::uint16_t)>(), v);
    }

    [[nodiscard]] constexpr ByteCount length() const noexcept { return ByteCount{v}; }

    friend constexpr bool operator==(NetWord, NetWord) = default;
};

struct OnesSum {
    std::uint32_t v = 0;

    constexpr void add(NetWord w) noexcept { v += w.v; }

    [[nodiscard]] constexpr NetWord complement() const noexcept {
        constexpr unsigned kHalfBits = 16;
        constexpr std::uint32_t kHalf = 0xFFFF;
        std::uint32_t s = v;
        while ((s >> kHalfBits) != 0)
            s = (s & kHalf) + (s >> kHalfBits);
        return NetWord{static_cast<std::uint16_t>(~s)};
    }
};

constexpr ByteCount kMinFrame{20};
constexpr ByteCount kMacBytes{svc::MacAddress::kBytes};
constexpr ByteCount kNetWordBytes{sizeof(std::uint16_t)};

constexpr ByteOffset kEtherType{12};
constexpr ByteOffset kPayload{14};
constexpr NetWord kArp{0x0806};
constexpr NetWord kIpv4{0x0800};

constexpr ByteOffset kArpHardware{0};
constexpr ByteOffset kArpHwLen{4};
constexpr ByteOffset kArpSender{8};
constexpr ByteOffset kArpTarget{18};
constexpr NetWord kArpEthernet{1};

constexpr ByteOffset kIpProto{9};
constexpr ByteOffset kIpSource{12};
constexpr ByteCount kIpAddresses{8};
constexpr std::byte kUdpProtocol{17};
constexpr std::byte kIhlMask{0x0F};

constexpr ByteOffset kUdpSource{0};
constexpr ByteOffset kUdpDest{2};
constexpr ByteOffset kUdpLength{4};
constexpr ByteOffset kUdpChecksum{6};
constexpr ByteCount kUdpHeader{8};
constexpr ByteOffset kDhcpChaddr{36};
constexpr NetWord kDhcpServer{67};
constexpr NetWord kDhcpClient{68};
constexpr std::byte kPad{};

[[nodiscard]] bool dhcp_port(NetWord p) noexcept {
    return p == kDhcpServer || p == kDhcpClient;
}

[[nodiscard]] ByteCount ip_header_bytes(std::byte version_ihl) noexcept {
    return ByteCount{std::to_integer<std::size_t>(version_ihl & kIhlMask) * sizeof(std::uint32_t)};
}

}

bool A2065MacMunge::swap_(std::span<std::byte> at) const noexcept {
    if (station_ == wire_)
        return true;
    if (station_.matches(at)) {
        wire_.write(at);
        return true;
    }
    if (wire_.matches(at)) {
        station_.write(at);
        return true;
    }
    return false;
}

void A2065MacMunge::munge(std::span<std::byte> f) const noexcept {
    if (ByteCount{f.size()} < kMinFrame || station_ == wire_)
        return;
    const NetWord type = NetWord::at(f, kEtherType);
    (void)swap_(f);
    (void)swap_(f.subspan(svc::MacAddress::kBytes));
    const std::span<std::byte> data = from(f, kPayload);

    const ByteOffset data_end{data.size()};
    const auto holds = [&](ByteOffset at, ByteCount n) {
        return at + n <= data_end;
    };
    if (type == kArp) {
        if (!holds(kArpTarget, kMacBytes))
            return;
        if (NetWord::at(data, kArpHardware) == kArpEthernet
            && std::to_integer<std::size_t>(from(data, kArpHwLen).front())
                == svc::MacAddress::kBytes) {
            (void)swap_(from(data, kArpSender));
            (void)swap_(from(data, kArpTarget));
        }
        return;
    }
    if (type != kIpv4 || !holds(kIpSource, kIpAddresses))
        return;
    const std::span<const std::byte> ip = data;
    if (from(ip, kIpProto).front() != kUdpProtocol)
        return;
    const ByteOffset udp_at = ByteOffset{} + ip_header_bytes(ip.front());
    if (!holds(udp_at, kUdpHeader))
        return;
    const std::span<std::byte> udp = from(data, udp_at);
    const ByteOffset udp_end{udp.size()};
    const NetWord length = NetWord::at(udp, kUdpLength);
    bool changed = false;
    if ((dhcp_port(NetWord::at(udp, kUdpSource)) || dhcp_port(NetWord::at(udp, kUdpDest)))
        && kDhcpChaddr + kMacBytes <= udp_end) {
        changed = swap_(from(udp, kDhcpChaddr));
    }
    const bool has_sum = NetWord::at(udp, kUdpChecksum) != NetWord{};

    const ByteOffset end = ByteOffset{} + length.length();
    if (!changed || !has_sum || end > udp_end)
        return;
    NetWord{}.put(udp, kUdpChecksum);
    if (end < udp_end)
        from(udp, end).front() = kPad;
    OnesSum sum{};
    for (ByteOffset i{}; i < end; i += kNetWordBytes)
        sum.add(NetWord::padded_at(udp, i));
    for (ByteOffset i = kIpSource; i < kIpSource + kIpAddresses; i += kNetWordBytes)
        sum.add(NetWord::at(ip, i));
    sum.add(NetWord::of_byte(kUdpProtocol));
    sum.add(length);
    sum.complement().put(udp, kUdpChecksum);
}

}
