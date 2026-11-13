// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::os {

class UnixStreamSocket {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<std::optional<UnixStreamSocket>> connect(
        std::string_view path
    ) noexcept;

    [[nodiscard]] const UniqueFd& fd() const noexcept { return fd_; }

    [[nodiscard]] Ex<std::span<const std::byte>> write_some(
        std::span<const std::byte> bytes
    ) noexcept;

    [[nodiscard]] Ex<std::optional<std::span<std::byte>>> read_some(
        std::span<std::byte> bytes
    ) noexcept;

private:
    explicit UnixStreamSocket(UniqueFd fd) noexcept : fd_(std::move(fd)) {}

    UniqueFd fd_;
};

}
