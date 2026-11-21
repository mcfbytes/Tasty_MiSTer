// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/positioned_io.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace mister::os {

static_assert(sizeof(::off_t) == sizeof(std::int64_t), "_FILE_OFFSET_BITS=64: warnings.cmake");

[[nodiscard]] Ex<infra::ByteCount> pread_at(
    const UniqueFd& fd,
    std::span<std::byte> dst,
    SeekOffset at
) noexcept {
    infra::ByteCount done{};
    while (!done.covers(dst)) {
        const std::span<std::byte> rest = done.past_in(dst);
        const ::ssize_t n = ::pread(fd.get(), rest.data(), rest.size(), at.past(done).v);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        if (n == 0)
            break;
        done += infra::ByteCount{static_cast<std::size_t>(n)};
    }
    return done;
}

[[nodiscard]] Ex<infra::ByteCount> pwrite_at(
    const UniqueFd& fd,
    std::span<const std::byte> src,
    SeekOffset at
) noexcept {
    infra::ByteCount done{};
    while (!done.covers(src)) {
        const std::span<const std::byte> rest = done.past_in(src);
        const ::ssize_t n = ::pwrite(fd.get(), rest.data(), rest.size(), at.past(done).v);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        if (n == 0) {
            return std::unexpected(done.refusal(Errc::short_write, ERR_SITE()));
        }
        done += infra::ByteCount{static_cast<std::size_t>(n)};
    }
    return done;
}

[[nodiscard]] Ex<void> advise(
    const UniqueFd& fd,
    SeekOffset at,
    SeekLength len,
    Advice advice
) noexcept {
    const int hint = advice == Advice::Sequential ? POSIX_FADV_SEQUENTIAL : POSIX_FADV_WILLNEED;
    const int rc = ::posix_fadvise(fd.get(), at.v, len.v, hint);
    if (rc != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), rc));
    return {};
}

}
