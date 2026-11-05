// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "proto/link_frame.h"

namespace mister::proto {

class SpiFrame final : public ILinkFrame {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit SpiFrame(hal::ISpiTransport& link) noexcept : link_(link) {}

    ~SpiFrame() override { close_(); }

    SpiFrame(const SpiFrame&) = delete;
    SpiFrame& operator=(const SpiFrame&) = delete;

    using ILinkFrame::begin;
    [[nodiscard]] Ex<void> begin(FrameSel sel, std::uint16_t op) override;
    [[nodiscard]] Ex<void> begin(LinkRoute route, std::uint16_t op) override;
    [[nodiscard]] Ex<void> word(std::uint16_t w) override;
    [[nodiscard]] Ex<void> end() override;
    [[nodiscard]] std::optional<std::uint16_t> opcode_reply() const noexcept override;

    [[nodiscard]] Ex<std::uint16_t> open(hal::ChipSelect cs, std::uint16_t op);
    [[nodiscard]] Ex<std::uint16_t> open(FrameSel sel, std::uint16_t op);
    [[nodiscard]] Ex<std::uint16_t> open(LinkRoute route, std::uint16_t op);

    [[nodiscard]] Ex<std::uint16_t> beat(std::uint16_t w);

    void abandon() noexcept { close_(); }

    [[nodiscard]] bool is_open() const noexcept { return open_; }

    [[nodiscard]] hal::Width width() const noexcept { return link_.width(); }

private:
    void close_() noexcept;

    hal::ISpiTransport& link_;
    bool open_ = false;
    std::optional<std::uint16_t> reply_{};
};

}
