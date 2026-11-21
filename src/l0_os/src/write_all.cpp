// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/write_all.h"

#include <unistd.h>

#include <cerrno>

#include "infra/prelude.h"

namespace mister::os {

namespace {

[[nodiscard]] constexpr off_t head_offset(std::size_t done) noexcept {
    return static_cast<off_t>(done);
}

template <class Step>
[[nodiscard]] Ex<void> transfer(std::span<const std::byte> bytes, Step step) noexcept {
    std::size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n = step(bytes.subspan(off));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        if (n == 0)
            return std::unexpected(Error{Errc::short_write, ERR_SITE(), 0});
        off += static_cast<std::size_t>(n);
    }
    return {};
}

}

[[nodiscard]] Ex<void> write_all(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept {
    return transfer(bytes, [&](std::span<const std::byte> rest) {
        return ::write(fd.get(), rest.data(), rest.size());
    });
}

[[nodiscard]] Ex<void> rewrite_head(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept {

    return transfer(bytes, [&](std::span<const std::byte> rest) {
        return ::pwrite(
            fd.get(),
            rest.data(),
            rest.size(),
            head_offset(bytes.size() - rest.size())
        );
    });
}

void write_all_best_effort(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept {

    (void)transfer(bytes, [&](std::span<const std::byte> rest) {
        return ::write(fd.get(), rest.data(), rest.size());
    });
}

}
