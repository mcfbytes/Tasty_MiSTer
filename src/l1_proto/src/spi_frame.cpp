// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_frame.h"

#include <array>
#include <cstddef>

namespace mister::proto {

namespace {

constexpr std::array<hal::ChipSelect, 4> kSelectOf{
    hal::ChipSelect::Io,
    hal::ChipSelect::Fpga,
    hal::ChipSelect::OsdHdmi,
    hal::ChipSelect::OsdVga
};
static_assert(
    kSelectOf[static_cast<std::size_t>(FrameSel::Uio)] == hal::ChipSelect::Io
    && kSelectOf[static_cast<std::size_t>(FrameSel::Fpga)] == hal::ChipSelect::Fpga
    && kSelectOf[static_cast<std::size_t>(FrameSel::OsdHdmi)] == hal::ChipSelect::OsdHdmi
    && kSelectOf[static_cast<std::size_t>(FrameSel::OsdVga)] == hal::ChipSelect::OsdVga
);

}

Ex<void> SpiFrame::begin(FrameSel sel, std::uint16_t op) {
    TASTY_SEAT_BODY(SpiFrame);
    close_();
    const auto i = static_cast<std::size_t>(sel);
    if (i >= kSelectOf.size()) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), static_cast<std::uint32_t>(i)});
    }
    link_.select(kSelectOf[i]);
    open_ = true;
    return put_(op);
}

Ex<void> SpiFrame::word(std::uint16_t w) {
    TASTY_SEAT_BODY(SpiFrame);
    return put_(w);
}

Ex<void> SpiFrame::end() {
    TASTY_SEAT_BODY(SpiFrame);
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    close_();
    return {};
}

Ex<void> SpiFrame::put_(std::uint16_t w) {
    if (!open_)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), w});
    if (auto r = link_.transfer(hal::SpiWord{w}); !r) {
        close_();
        return std::unexpected(r.error());
    }
    return {};
}

void SpiFrame::close_() noexcept {
    if (!open_)
        return;
    open_ = false;
    link_.deselect();
}

}
