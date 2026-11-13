// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/hci_reset.h"

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <cerrno>

#include "infra/unique_fd.h"

namespace mister::os {
Ex<HciResetOutcome> HciReset::reset(HciIndex index) noexcept {
    const int fd = ::socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, kProtocolHci);
    if (fd < 0) {
        const int e = errno;
        if (e == EAFNOSUPPORT
            || e == EPROTONOSUPPORT
            || e == EPFNOSUPPORT
            || e == EINVAL
            || e == ENOENT) {
            return HciResetOutcome::NoAdapter;
        }
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), e));
    }
    UniqueFd guard{fd};
    if (::ioctl(fd, _IOW('H', kRequestDevReset, int), index.ioctl_arg()) < 0) {
        const int e = errno;
        if (e == ENODEV || e == ENOENT)
            return HciResetOutcome::NoAdapter;
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), e));
    }
    return HciResetOutcome::Reset;
}

}
