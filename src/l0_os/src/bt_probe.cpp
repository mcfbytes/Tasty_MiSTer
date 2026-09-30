// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/bt_probe.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>

#include "infra/unique_fd.h"

namespace mister::os {
namespace {

constexpr int kAfBluetooth = 31;
constexpr int kBtprotoHci = 1;
constexpr unsigned long kHciGetDevList = 0x800448D2UL;
constexpr unsigned kHciUpBit = 0;

struct HciDevReq {
    std::uint16_t dev_id;
    std::uint32_t dev_opt;
};

constexpr std::size_t kHciMaxDev = 16;
struct HciDevListReq {
    std::uint16_t dev_num;
    std::uint16_t pad;
    HciDevReq req[kHciMaxDev];
};

}  // namespace

bool hci_devlist_any_up(std::span<const std::byte> reply) noexcept {
    constexpr std::size_t kHdrBytes = 4;
    constexpr std::size_t kReqBytes = 8;
    constexpr std::size_t kOptOff = 4;
    if (reply.size() < kHdrBytes) return false;
    const auto byte_at = [&](std::size_t i) {
        return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(reply[i]));
    };
    std::size_t n = byte_at(0) | (byte_at(1) << 8);

    const std::size_t fits = (reply.size() - kHdrBytes) / kReqBytes;
    if (n > fits) n = fits;
    if (n > kHciMaxDev) n = kHciMaxDev;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t o = kHdrBytes + i * kReqBytes + kOptOff;
        const std::uint32_t opt =
            byte_at(o) | (byte_at(o + 1) << 8) | (byte_at(o + 2) << 16) | (byte_at(o + 3) << 24);
        if ((opt & (1u << kHciUpBit)) != 0u) return true;
    }
    return false;
}

Ex<bool> bluetooth_adapter_up() {
    const int fd = ::socket(kAfBluetooth, SOCK_RAW | SOCK_CLOEXEC, kBtprotoHci);
    if (fd < 0) {
        const int e = errno;

        if (e == EAFNOSUPPORT || e == EPROTONOSUPPORT || e == EPFNOSUPPORT || e == EINVAL ||
            e == ENOENT) {
            return false;
        }

        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    UniqueFd guard{fd};

    HciDevListReq dl{};
    dl.dev_num = static_cast<std::uint16_t>(kHciMaxDev);
    if (::ioctl(fd, kHciGetDevList, &dl) < 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    return hci_devlist_any_up(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&dl), sizeof dl));
}

}  // namespace mister::os
