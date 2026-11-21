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
#include "infra/file_mode.h"
#include "infra/prelude.h"
#include "os/file_create.h"
#include "os/write_all.h"

namespace mister::fw {

TastyOwnerLock::TastyOwnerLock(TastyPaths paths, UniqueFd fd) noexcept
    : paths_(paths),
      fd_(std::move(fd)) {}

Ex<TastyOwnerLock> TastyOwnerLock::take(const TastyPaths& paths) noexcept {
    constexpr os::OpenFlags kLockFlags = os::OpenFlags::read_write().creating();
    static_assert(kLockFlags.open_word() == (O_RDWR | O_CREAT | O_CLOEXEC));
    auto fd = os::open_created(paths.lock.c_str(), kLockFlags, infra::kSharedFileMode);
    if (!fd)
        return std::unexpected(fd.error().refusal(Errc::os, ERR_SITE()));
    if (::flock(fd->get(), LOCK_EX | LOCK_NB) != 0) {
        return std::unexpected(infra::errno_error(Errc::busy, ERR_SITE(), errno));
    }
    return TastyOwnerLock{paths, std::move(*fd)};
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
    constexpr os::OpenFlags kPidFlags = os::OpenFlags::write_only().creating().truncating();
    static_assert(kPidFlags.open_word() == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC));
    auto fd = os::open_created(paths_.pid.c_str(), kPidFlags, infra::kSharedFileMode);
    if (!fd)
        return std::unexpected(fd.error().refusal(Errc::os, ERR_SITE()));
    const UniqueFd guard{std::move(*fd)};

    const std::string bytes = std::to_string(pid.printed()) + "\n" + std::string(movie) + "\n";
    return os::write_all(guard, std::as_bytes(std::span<const char>(bytes)));
}

}
