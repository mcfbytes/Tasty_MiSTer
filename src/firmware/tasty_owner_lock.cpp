// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_owner_lock.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <span>
#include <string>
#include <utility>

#include "app/path_text.h"
#include "infra/error.h"
#include "os/write_all.h"

namespace mister::fw {

TastyOwnerLock::TastyOwnerLock(TastyPaths paths, UniqueFd fd) noexcept
    : paths_(paths),
      fd_(std::move(fd)) {}

Ex<TastyOwnerLock> TastyOwnerLock::take(const TastyPaths& paths) noexcept {
    const int fd = ::open(
        paths.lock.c_str(),
        O_RDWR | O_CREAT | O_CLOEXEC,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
    );
    if (fd < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        ::close(fd);
        return std::unexpected(infra::errno_error(Errc::busy, ERR_SITE(), e));
    }
    return TastyOwnerLock{paths, UniqueFd{fd}};
}

void TastyOwnerLock::release() noexcept {
    if (!fd_.valid())
        return;
    (void)::unlink(paths_.pid.c_str());
    (void)::unlink(paths_.status.c_str());
    if (const auto tmp = paths_.status_tmp())
        (void)::unlink(tmp->c_str());
    (void)::unlink(paths_.lock.c_str());
    (void)::flock(fd_.get(), LOCK_UN);
    fd_.reset();
}

Ex<void> TastyOwnerLock::write_pid(os::ProcessId pid, std::string_view movie) const noexcept {
    const int fd = ::open(
        paths_.pid.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
    );
    if (fd < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    const UniqueFd guard{fd};

    const std::string bytes = std::to_string(pid.printed()) + "\n" + std::string(movie) + "\n";
    return os::write_all(guard, std::as_bytes(std::span<const char>(bytes)));
}

}
