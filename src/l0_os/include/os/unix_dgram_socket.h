// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/socket_group.h"

namespace mister::os {

class UnixDgramSocket {
    TASTY_SEAT_EXEMPT(component);

public:
    enum : std::size_t { kPathMax = 108 };

    enum : std::uint32_t { kClientUmask = 007u };

    [[nodiscard]] static Ex<UnixDgramSocket> bind_client(
        std::string_view dir_prefix,
        SocketGroup group
    ) noexcept;

    UnixDgramSocket(UnixDgramSocket&&) noexcept = default;
    UnixDgramSocket& operator=(UnixDgramSocket&&) = delete;
    UnixDgramSocket(const UnixDgramSocket&) = delete;
    UnixDgramSocket& operator=(const UnixDgramSocket&) = delete;
    ~UnixDgramSocket();

    [[nodiscard]] Ex<void> connect(std::string_view path) noexcept;

    [[nodiscard]] const UniqueFd& fd() const noexcept { return fd_; }

    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }

    [[nodiscard]] Ex<bool> send(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] Ex<std::optional<std::span<std::byte>>> recv(std::span<std::byte> bytes) noexcept;

private:
    UnixDgramSocket(UniqueFd fd, infra::PathText path) noexcept;

    UniqueFd fd_;
    infra::PathText path_{};
};

}
