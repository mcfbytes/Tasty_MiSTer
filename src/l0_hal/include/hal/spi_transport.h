// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/types.h"

namespace mister::hal {

enum class ChipSelect : std::uint8_t {
    None = 0,
    Fpga = 1,
    Osd = 2,
    Io = 3,

    OsdVga = 4,
    OsdHdmi = 5,
};

class ISpiTransport {
public:
    virtual ~ISpiTransport() = default;
    ISpiTransport() = default;
    ISpiTransport(const ISpiTransport&) = delete;
    ISpiTransport& operator=(const ISpiTransport&) = delete;

protected:
    ISpiTransport(ISpiTransport&&) = default;
    ISpiTransport& operator=(ISpiTransport&&) = default;

public:
    virtual void select(ChipSelect cs) = 0;
    virtual void deselect() = 0;

    virtual Ex<SpiWord> transfer(SpiWord out) = 0;

    virtual Width width() const noexcept { return Width::Byte; }
    virtual std::uint8_t io_version() const noexcept { return 0; }

    virtual Ex<void> block_write(std::span<const std::uint8_t> data) = 0;
    virtual Ex<void> block_read(std::span<std::uint8_t> data) = 0;

    [[nodiscard]] virtual Ex<void> post(SpiWord out) {
        return transfer(out).transform([](SpiWord) noexcept {});
    }

    [[nodiscard]] virtual Ex<bool> posted_done() { return true; }

    virtual void abandon_post() noexcept {}
};

}  // namespace mister::hal
