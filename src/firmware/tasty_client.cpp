// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_client.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "os/small_file.h"
#include "proto/types.h"
#include "tasty_pid_file.h"

namespace mister::fw {

bool TastyClient::owner_live() const noexcept {
    const auto pid = TastyPidFile{paths_}.pid();
    return pid && owner_.names(*pid) && pid->presence() == os::ProcessId::Presence::Alive;
}

Ex<void> TastyClient::signal_owner() const noexcept {
    const auto pid = TastyPidFile{paths_}.pid();
    if (!pid || !owner_.names(*pid))
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    return pid->terminate();
}

Ex<void> TastyClient::send(std::string_view line) const noexcept {
    const int fd = ::open(paths_.cmd.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    const UniqueFd guard{fd};
    char buf[1024];
    if (line.size() >= sizeof buf - 1) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), proto::FileSize{line.size()}.detail()}
        );
    }
    std::memcpy(buf, line.data(), line.size());
    buf[line.size()] = '\n';
    const ssize_t w = ::write(guard.get(), buf, line.size() + 1);
    if (w < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return {};
}

Ex<std::string> TastyClient::status() const noexcept {
    std::array<char, 1024> buf{};
    const Ex<std::span<char>> got = os::read_small_file(paths_.status.c_str(), buf);
    if (!got)
        return std::unexpected(got.error());
    return std::string(got->data(), got->size());
}

}
