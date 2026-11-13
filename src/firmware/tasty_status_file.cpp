// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_status_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <span>
#include <string>
#include <string_view>

#include "infra/unique_fd.h"
#include "os/write_all.h"

namespace mister::fw {

Ex<void> TastyStatusFile::write(std::string_view json) const noexcept {
    const auto tmp = paths_.status_tmp();
    if (!tmp)
        return std::unexpected(tmp.error());
    const int fd = ::open(
        tmp->c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
    );
    if (fd < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    const UniqueFd guard{fd};
    if (const auto wrote = os::write_all(guard, std::as_bytes(std::span<const char>(json)));
        !wrote) {
        (void)::unlink(tmp->c_str());
        return std::unexpected(wrote.error());
    }
    if (::rename(tmp->c_str(), paths_.status.c_str()) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return {};
}

}
