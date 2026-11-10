// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/write_all.h"

#include <unistd.h>

#include <cerrno>
#include <optional>

namespace mister::os {

namespace {

[[nodiscard]] Ex<void> transfer(
    const UniqueFd& fd,
    std::span<const std::byte> bytes,
    std::optional<FileOffset> at
) noexcept {
    std::size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n = at ? ::pwrite(
                                   fd.get(),
                                   bytes.data() + off,
                                   bytes.size() - off,
                                   at->at(static_cast<std::uint64_t>(off))
                               )
                             : ::write(fd.get(), bytes.data() + off, bytes.size() - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        if (n == 0)
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), EIO));
        off += static_cast<std::size_t>(n);
    }
    return {};
}

}

[[nodiscard]] Ex<void> write_all(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept {
    return transfer(fd, bytes, std::nullopt);
}

[[nodiscard]] Ex<void> rewrite_head(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept {

    constexpr FileOffset kHeadOffset{0};
    return pwrite_all(fd, bytes, kHeadOffset);
}

[[nodiscard]] Ex<void> pwrite_all(
    const UniqueFd& fd,
    std::span<const std::byte> bytes,
    std::optional<FileOffset> offset
) noexcept {
    return transfer(fd, bytes, offset);
}

}
