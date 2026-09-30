// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "cores/pcm_feed.h"
#include "infra/error.h"

namespace mister::cores {

class ICoreWindow {
public:
    virtual ~ICoreWindow() = default;

    [[nodiscard]] virtual Ex<std::size_t> write(std::size_t off,
                                                std::span<const std::byte> src) = 0;
    [[nodiscard]] virtual Ex<std::size_t> read(std::size_t off, std::span<std::byte> dst) const = 0;

    virtual void publish() noexcept = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] virtual IPcmFeed* pcm_feed() noexcept { return nullptr; }

protected:
    ICoreWindow() = default;
    ICoreWindow(const ICoreWindow&) = default;
    ICoreWindow& operator=(const ICoreWindow&) = default;
};

}  // namespace mister::cores
