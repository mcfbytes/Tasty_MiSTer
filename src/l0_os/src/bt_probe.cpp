// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/bt_probe.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_view.h"
#include "infra/endian.h"
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

constexpr std::uint16_t kHciMaxDev = 16;

struct HciDevListReq {
    std::uint16_t dev_num;
    std::uint16_t pad;
    HciDevReq req[kHciMaxDev];
};

static_assert(sizeof(HciDevReq) == 8, "the kernel's hci_dev_req is a u16 id + pad + u32 opt");
static_assert(offsetof(HciDevReq, dev_opt) == 4, "dev_opt sits at offset 4 in hci_dev_req");
static_assert(offsetof(HciDevListReq, req) == 4, "the entry array starts after dev_num and pad");

}

bool hci_devlist_any_up(std::span<const std::byte> reply) noexcept {
    constexpr std::size_t kHdrBytes = offsetof(HciDevListReq, req);
    constexpr std::size_t kReqBytes = sizeof(HciDevReq);
    constexpr std::size_t kOptOff = offsetof(HciDevReq, dev_opt);
    if (reply.size() < kHdrBytes)
        return false;

    std::size_t n = infra::load_le<std::uint16_t>(reply.first<2>());

    const std::size_t fits = (reply.size() - kHdrBytes) / kReqBytes;
    if (n > fits)
        n = fits;
    if (n > kHciMaxDev)
        n = kHciMaxDev;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t o = kHdrBytes + i * kReqBytes + kOptOff;
        const std::uint32_t opt = infra::load_le<std::uint32_t>(reply.subspan(o).first<4>());
        if ((opt & (1u << kHciUpBit)) != 0u)
            return true;
    }
    return false;
}

[[nodiscard]] Ex<bool> bluetooth_adapter_up() {
    const int fd = ::socket(kAfBluetooth, SOCK_RAW | SOCK_CLOEXEC, kBtprotoHci);
    if (fd < 0) {
        const int e = errno;

        if (e == EAFNOSUPPORT
            || e == EPROTONOSUPPORT
            || e == EPFNOSUPPORT
            || e == EINVAL
            || e == ENOENT) {
            return false;
        }

        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), e));
    }
    UniqueFd guard{fd};

    HciDevListReq dl{};
    dl.dev_num = kHciMaxDev;
    if (::ioctl(fd, kHciGetDevList, &dl) < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }

    return hci_devlist_any_up(infra::bytes_of(dl));
}

}
