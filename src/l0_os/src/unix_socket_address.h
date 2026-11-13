// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/socket.h>
#include <sys/un.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>

namespace mister::os {

struct UnixSocketAddress {
    sockaddr header{};
    std::array<char, sizeof(sockaddr_un::sun_path) - sizeof(sockaddr::sa_data)> path_tail{};

    [[nodiscard]] static std::optional<UnixSocketAddress> of(std::string_view path) noexcept {
        constexpr std::size_t kSunPathMax = sizeof(sockaddr_un::sun_path);
        constexpr std::size_t kHeaderPath = sizeof(sockaddr::sa_data);
        if (path.empty() || path.size() >= kSunPathMax)
            return std::nullopt;
        UnixSocketAddress address{};
        address.header.sa_family = AF_UNIX;
        const std::size_t in_header = path.size() < kHeaderPath ? path.size() : kHeaderPath;
        std::memcpy(address.header.sa_data, path.data(), in_header);
        std::memcpy(address.path_tail.data(), path.data() + in_header, path.size() - in_header);

        return address;
    }

    [[nodiscard]] const sockaddr* abi() const noexcept { return &header; }
};

static_assert(
    offsetof(sockaddr_un, sun_path) == offsetof(sockaddr, sa_data),
    "sun_path must continue where the sockaddr header's sa_data begins"
);
static_assert(
    offsetof(UnixSocketAddress, path_tail) == sizeof(sockaddr),
    "path_tail must start where the sockaddr header ends"
);
static_assert(
    sizeof(UnixSocketAddress) == sizeof(sockaddr_un),
    "the address must be exactly one sockaddr_un"
);

}
