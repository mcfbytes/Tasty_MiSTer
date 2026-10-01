// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/rec_options.h"
#include "infra/error.h"
#include "os/clock.h"

namespace mister::app {

class IFrameCodec {
public:
    virtual ~IFrameCodec() = default;

    struct Search {
        std::uint8_t radius = 0;
        std::uint32_t cut = 0;
    };

    virtual void restart(const RecOptions& opt) noexcept = 0;
    virtual void arm(const os::IClock* clock, std::int64_t budget_ns) noexcept = 0;
    [[nodiscard]] virtual Search search() const noexcept = 0;
    [[nodiscard]] virtual Ex<void> begin(std::uint16_t w, std::uint16_t h) noexcept = 0;
    virtual void end() noexcept = 0;
    [[nodiscard]] virtual bool open() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t height() const noexcept = 0;
    [[nodiscard]] virtual std::size_t max_payload() const noexcept = 0;
    [[nodiscard]] virtual std::size_t encode(const std::byte* rgb, std::size_t line, bool key,
                                             std::span<std::byte> out) noexcept = 0;
    [[nodiscard]] virtual std::size_t rekey(std::span<std::byte> out) noexcept = 0;

    [[nodiscard]] virtual std::span<const std::byte> dup() noexcept = 0;
};

}  // namespace mister::app
