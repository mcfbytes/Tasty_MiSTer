// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/sync.h"

#include <unistd.h>

#include <cerrno>

#include "infra/prelude.h"

namespace mister::os {

[[nodiscard]] Ex<void> sync(const UniqueFd& fd, SyncKind kind) noexcept {
    const int rc = kind == SyncKind::Data ? ::fdatasync(fd.get()) : ::fsync(fd.get());
    if (rc == 0)
        return {};
    const int e = errno;

    if (e == EINVAL || e == EROFS)
        return {};
    return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), e));
}

}
