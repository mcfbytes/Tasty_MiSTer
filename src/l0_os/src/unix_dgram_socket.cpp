// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/unix_dgram_socket.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>
#include <utility>

#include "infra/decimal_text.h"
#include "infra/errno.h"
#include "unix_socket_address.h"

namespace mister::os {
namespace {

constexpr char kSep = '_';

enum : uid_t { kKeepOwner = ~uid_t{0} };

[[nodiscard]] Error bad_format() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] Error errno_at(infra::Errno err) noexcept {
    return err.refused(Errc::os, ERR_SITE());
}

[[nodiscard]] bool absent_errno(infra::Errno err) noexcept {
    return err == infra::Errno{ENOENT} || err == infra::Errno{ECONNREFUSED};
}

[[nodiscard]] UniqueFd open_dgram() noexcept {
    return UniqueFd{::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
}

}

UnixDgramSocket::UnixDgramSocket(UniqueFd fd, infra::PathText path) noexcept
    : fd_(std::move(fd)),
      path_(path) {}

UnixDgramSocket::~UnixDgramSocket() {
    if (fd_.valid())
        (void)::unlink(path_.c_str());
}

Ex<UnixDgramSocket> UnixDgramSocket::bind_client(
    std::string_view dir_prefix,
    SocketGroup group
) noexcept {
    UniqueFd fd = open_dgram();
    if (!fd.valid())
        return std::unexpected(errno_at(infra::Errno{errno}));

    std::array<char, kPathMax> path{};
    if (dir_prefix.empty() || dir_prefix.size() >= path.size()) {
        return std::unexpected(bad_format());
    }
    std::memcpy(path.data(), dir_prefix.data(), dir_prefix.size());
    std::size_t at = dir_prefix.size();

    const auto append_field = [&path, &at](std::string_view field) noexcept {
        if (at + 1 + field.size() >= path.size())
            return false;
        path[at++] = kSep;
        std::memcpy(path.data() + at, field.data(), field.size());
        at += field.size();
        return true;
    };
    const infra::DecimalText pid{::getpid()};
    const infra::DecimalText number{fd.get()};
    if (!append_field(pid.view()) || !append_field(number.view())) {
        return std::unexpected(bad_format());
    }
    path[at] = '\0';
    const std::string_view path_view{path.data(), at};

    const auto address = UnixSocketAddress::of(path_view);
    if (!address)
        return std::unexpected(bad_format());
    const mode_t previous = ::umask(kClientUmask);
    const int bound = ::bind(fd.get(), address->abi(), sizeof(*address));
    const infra::Errno bind_errno{errno};
    (void)::umask(previous);
    if (bound != 0)
        return std::unexpected(errno_at(bind_errno));

    if (group.chowns() && ::lchown(path.data(), kKeepOwner, group.gid().value()) != 0) {
        const infra::Errno chown_errno{errno};
        (void)::unlink(path.data());
        return std::unexpected(errno_at(chown_errno));
    }

    infra::PathText stored;
    if (!stored.assign(path_view))
        return std::unexpected(bad_format());
    return UnixDgramSocket{std::move(fd), stored};
}

Ex<void> UnixDgramSocket::connect(std::string_view path) noexcept {
    const auto address = UnixSocketAddress::of(path);
    if (!address)
        return std::unexpected(bad_format());
    if (::connect(fd_.get(), address->abi(), sizeof(*address)) != 0) {
        const infra::Errno err{errno};
        if (absent_errno(err))
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        return std::unexpected(errno_at(err));
    }
    return {};
}

Ex<bool> UnixDgramSocket::send(std::span<const std::byte> bytes) noexcept {
    const ssize_t n = ::send(fd_.get(), bytes.data(), bytes.size(), 0);
    if (n >= 0)
        return true;
    const infra::Errno err{errno};
    if (err == infra::Errno{EAGAIN} || err == infra::Errno{EWOULDBLOCK})
        return false;
    if (absent_errno(err))
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    return std::unexpected(errno_at(err));
}

Ex<std::optional<std::span<std::byte>>> UnixDgramSocket::recv(std::span<std::byte> bytes) noexcept {
    const ssize_t n = ::recv(fd_.get(), bytes.data(), bytes.size(), 0);
    if (n < 0) {
        const infra::Errno err{errno};
        if (err == infra::Errno{EAGAIN} || err == infra::Errno{EWOULDBLOCK})
            return std::nullopt;
        if (absent_errno(err))
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        return std::unexpected(errno_at(err));
    }
    return std::optional<std::span<std::byte>>{
        std::span<std::byte>{bytes.data(), bytes.data() + n}
    };
}

}
