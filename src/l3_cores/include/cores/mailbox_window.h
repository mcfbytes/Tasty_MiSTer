// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "infra/error.h"

namespace mister::cores {

class MailboxWindow {
public:
    MailboxWindow() = default;

    explicit MailboxWindow(std::span<std::byte> window) noexcept : window_(window) {

        if (window.size() > std::numeric_limits<std::uint32_t>::max())
            window_ = {};
    }

    [[nodiscard]] Ex<std::span<std::byte>> at(std::size_t off, std::size_t n) const;

    [[nodiscard]] std::size_t size() const noexcept { return window_.size(); }

    [[nodiscard]] bool empty() const noexcept { return window_.empty(); }

private:
    std::span<std::byte> window_{};
};

}
