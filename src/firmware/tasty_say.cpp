// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_say.h"

#include <poll.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>

namespace mister::fw {

namespace {

void write_stderr(std::string_view bytes) noexcept {
    if (bytes.empty())
        return;
    pollfd pf{};
    pf.fd = STDERR_FILENO;
    pf.events = POLLOUT;
    if (::poll(&pf, 1, 0) <= 0)
        return;
    if ((pf.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        return;
    const ssize_t w = ::write(STDERR_FILENO, bytes.data(), bytes.size());
    (void)w;
}

}

namespace detail {

void emit(std::span<char> buf, std::size_t n) noexcept {
    if (n < buf.size() && (n == 0 || buf[n - 1] != '\n'))
        buf[n++] = '\n';
    write_stderr(std::string_view{buf.data(), n});
}

}

void tasty_say(std::string_view line) noexcept {
    TastyLineBuf buf;
    std::size_t n = line.size();
    if (n >= buf.size())
        n = buf.size() - 1;
    if (n != 0)
        std::memcpy(buf.data(), line.data(), n);
    detail::emit(buf, n);
}

}
