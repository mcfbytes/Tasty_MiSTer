// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/link_frame.h"
#include "proto/spi_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

class Link final {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit Link(hal::ISpiTransport& link) noexcept : frame_(link) {}

    ~Link() { frame_.abandon(); }

    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    [[nodiscard]] Ex<std::uint16_t> command(hal::ChipSelect cs, std::uint16_t op);
    [[nodiscard]] Ex<std::uint16_t> command(FrameSel sel, std::uint16_t op);

    [[nodiscard]] Ex<std::uint16_t> command(LinkRoute route, std::uint16_t op);

    [[nodiscard]] Ex<std::uint16_t> command(UioOp op);

    [[nodiscard]] Ex<void> word(std::uint16_t w);

    [[nodiscard]] Ex<void> write_byte(std::uint8_t b) { return word(b); }

    [[nodiscard]] Ex<std::uint16_t> read();
    [[nodiscard]] Ex<std::uint32_t> read_dword();

    void close() noexcept;
    [[nodiscard]] bool open() const noexcept;

    [[nodiscard]] ILinkFrame& frame() noexcept { return frame_; }

    [[nodiscard]] hal::Width width() const noexcept { return frame_.width(); }

private:
    SpiFrame frame_;
};

}
