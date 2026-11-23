// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/spi_halves.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link_frame.h"
#include "proto/link/spi_frame.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto::link {

struct PollFrameRead;

class Link final {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit Link(hal::ISpiTransport& link) noexcept : frame_(link) {}

    ~Link() { frame_.abandon(); }

    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    [[nodiscard]] Ex<hal::SpiWord> command(LinkSel sel, hal::SpiWord op);

    [[nodiscard]] Ex<hal::SpiWord> command(UioOp op);

    [[nodiscard]] Ex<void> word(hal::SpiWord w);

    [[nodiscard]] Ex<void> write_byte(std::uint8_t b) { return word(hal::SpiWord{b}); }

    [[nodiscard]] Ex<hal::SpiWord> read();
    [[nodiscard]] Ex<hal::SpiHalves> read_dword();

    [[nodiscard]] Ex<hal::SpiWord> exchange(hal::SpiWord w);

    [[nodiscard]] PollFrameRead read_poll_frame(
        const user_io::PollFrameShape& shape,
        std::optional<ReplyWord> last_seen
    ) noexcept;

    void close() noexcept;
    [[nodiscard]] bool open() const noexcept;

    [[nodiscard]] ILinkFrame& frame() noexcept { return frame_; }

    [[nodiscard]] hal::Width width() const noexcept { return frame_.width(); }

private:
    [[nodiscard]] bool receive_(
        const user_io::PollFrameShape::Row& row,
        ReplyCount receives,
        PollFrameRead& out
    ) noexcept;
    SpiFrame frame_;
};

}
