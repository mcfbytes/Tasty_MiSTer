// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/unix_stream_socket.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <string_view>

#include "infra/errno.h"
#include "unix_socket_address.h"

namespace mister::os {
namespace {

[[nodiscard]] Error bad_format() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] Error errno_at(infra::Errno err) noexcept {
    return err.refused(Errc::os, ERR_SITE());
}

[[nodiscard]] UniqueFd open_stream() noexcept {
    return UniqueFd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
}

}

Ex<std::optional<UnixStreamSocket>> UnixStreamSocket::connect(std::string_view path) noexcept {
    UniqueFd fd = open_stream();
    if (!fd.valid())
        return std::unexpected(errno_at(infra::Errno{errno}));
    const auto address = UnixSocketAddress::of(path);
    if (!address)
        return std::unexpected(bad_format());
    if (::connect(fd.get(), address->abi(), sizeof(*address)) != 0) {
        const infra::Errno err{errno};
        if (err == infra::Errno{ENOENT} || err == infra::Errno{ECONNREFUSED}) {
            return std::optional<UnixStreamSocket>{};
        }
        return std::unexpected(errno_at(err));
    }
    return std::optional<UnixStreamSocket>{UnixStreamSocket{std::move(fd)}};
}

Ex<std::span<const std::byte>> UnixStreamSocket::write_some(
    std::span<const std::byte> bytes
) noexcept {
    const ssize_t n = ::send(fd_.get(), bytes.data(), bytes.size(), MSG_NOSIGNAL);
    if (n < 0) {
        const infra::Errno err{errno};
        if (err == infra::Errno{EAGAIN} || err == infra::Errno{EWOULDBLOCK}) {
            return std::span<const std::byte>{};
        }
        return std::unexpected(errno_at(err));
    }

    return std::span<const std::byte>{bytes.data(), bytes.data() + n};
}

Ex<std::optional<std::span<std::byte>>> UnixStreamSocket::read_some(
    std::span<std::byte> bytes
) noexcept {
    const ssize_t n = ::recv(fd_.get(), bytes.data(), bytes.size(), 0);
    if (n < 0) {
        const infra::Errno err{errno};
        if (err == infra::Errno{EAGAIN} || err == infra::Errno{EWOULDBLOCK})
            return std::nullopt;
        return std::unexpected(errno_at(err));
    }
    return std::optional<std::span<std::byte>>{
        std::span<std::byte>{bytes.data(), bytes.data() + n}
    };
}

}
