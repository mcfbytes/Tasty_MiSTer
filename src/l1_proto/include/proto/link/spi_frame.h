// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/spi_transport.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link_frame.h"

namespace mister::proto::link {

class SpiFrame final : public ILinkFrame {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit SpiFrame(hal::ISpiTransport& link) noexcept : link_(link) {}

    ~SpiFrame() override { close_(); }

    SpiFrame(const SpiFrame&) = delete;
    SpiFrame& operator=(const SpiFrame&) = delete;

    using ILinkFrame::begin;
    [[nodiscard]] Ex<void> begin(LinkSel sel, hal::LinkWord op) override;
    [[nodiscard]] Ex<void> word(hal::LinkWord w) override;
    [[nodiscard]] Ex<void> end() override;
    [[nodiscard]] std::optional<hal::LinkWord> opcode_reply() const noexcept override;

    [[nodiscard]] Ex<hal::LinkWord> open(LinkSel sel, hal::LinkWord op);

    [[nodiscard]] Ex<hal::LinkWord> beat(hal::LinkWord w);

    void abandon() noexcept { close_(); }

    [[nodiscard]] bool is_open() const noexcept { return open_; }

    [[nodiscard]] hal::Width width() const noexcept { return link_.width(); }

private:
    [[nodiscard]] Ex<hal::LinkWord> open_raw_(hal::ChipSelect cs, hal::LinkWord op);
    void close_() noexcept;

    hal::ISpiTransport& link_;
    bool open_ = false;
    std::optional<hal::LinkWord> reply_{};
};

}
