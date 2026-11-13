// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/osd_transport.h"

#include <cstdint>

#include "proto/osd_opcodes.h"

namespace mister::proto {

namespace {

constexpr std::uint16_t kModeMask = 0x0A;

}

hal::ChipSelect OsdTransport::chip_select() const noexcept {
    switch (target_) {
        case OsdTarget::Vga:
            return hal::ChipSelect::OsdVga;
        case OsdTarget::Hdmi:
            return hal::ChipSelect::OsdHdmi;
        case OsdTarget::All:
            break;
    }
    return hal::ChipSelect::Osd;
}

Ex<void> OsdTransport::command(proto::Link& link, std::uint16_t cmd) {
    if (auto r = link.command(chip_select(), cmd); !r)
        return std::unexpected(r.error());
    link.close();
    return {};
}

Ex<void> OsdTransport::flush_row(proto::Link& link, OsdRow r, std::span<const std::uint8_t> bytes) {
    if (!OsdSurface::kMaxRows.covers(r)) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.v});
    }
    if (bytes.size() != OsdSurface::kRowBytes) {
        return std::unexpected(OsdRowBytes::of(bytes).refused(Errc::bad_format, ERR_SITE()));
    }

    const auto cmd = osd_word(OsdOp::Write, r.wire_word());
    if (auto e = link.command(chip_select(), cmd); !e)
        return std::unexpected(e.error());
    for (const std::uint8_t b : bytes) {
        if (auto e = link.write_byte(b); !e)
            return e;
    }
    link.close();
    return {};
}

Ex<void> OsdTransport::set_visible(proto::Link& link, bool visible) {
    return visible ? enable(link, OsdMode::DisableKeyboard)
                   : command(link, osd_word(OsdOp::Disable, 0));
}

Ex<void> OsdTransport::enable(proto::Link& link, OsdMode mode) {

    const auto bits = static_cast<std::uint16_t>(mode);
    const auto m = static_cast<std::uint16_t>(bits & kModeMask);
    return command(link, osd_word(OsdOp::Enable, m));
}

Ex<void> OsdTransport::enable_info(proto::Link& link, InfoWindow window) {

    if (auto e = link.command(chip_select(), osd_word(OsdOp::EnableInfo, 0)); !e) {
        return std::unexpected(e.error());
    }
    for (const std::uint16_t word : window.wire_words()) {
        if (auto e = link.word(word); !e)
            return e;
    }
    link.close();
    return {};
}

Ex<void> OsdTransport::set_rotation(proto::Link& link, OsdRotation rotate) {

    if (auto e = link.command(chip_select(), osd_word(OsdOp::Disable, 0)); !e) {
        return std::unexpected(e.error());
    }
    constexpr unsigned kDummyWords = 4;
    for (unsigned i = 0; i < kDummyWords; ++i) {
        if (auto e = link.word(0); !e)
            return e;
    }

    for (const std::uint16_t word : rotation_words(rotate)) {
        if (auto e = link.word(word); !e)
            return e;
    }
    link.close();
    return {};
}

Ex<void> OsdTransport::menu_ctl(proto::Link& link, bool enable_menu) {

    if (!enable_menu)
        return command(link, osd_word(OsdOp::Disable, 0));
    if (auto e = command(link, osd_word(OsdOp::WriteLatch, 0)); !e)
        return e;
    return command(link, osd_word(OsdOp::Enable, 0));
}

}
