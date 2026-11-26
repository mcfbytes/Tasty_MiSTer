// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/osd_transport.h"

#include <cstdint>

#include "infra/prelude.h"
#include "proto/link_sel.h"
#include "proto/osd_opcodes.h"

namespace mister::proto::link {

namespace {

constexpr hal::LinkWord kModeMask{0x0A};

}

LinkSel OsdTransport::sel() const noexcept {
    switch (target_) {
        case OsdTarget::Vga:
            return LinkSel::OsdVga;
        case OsdTarget::Hdmi:
            return LinkSel::OsdHdmi;
        case OsdTarget::All:
            break;
    }
    return LinkSel::Osd;
}

Ex<void> OsdTransport::command(ILinkFrame& frame, hal::LinkWord cmd) {
    if (auto r = frame.begin(sel(), cmd); !r)
        return std::unexpected(r.error());
    return frame.end();
}

Ex<void> OsdTransport::flush_row(ILinkFrame& frame, OsdRow r, std::span<const std::uint8_t> bytes) {
    if (!OsdSurface::kMaxRows.covers(r)) {
        return std::unexpected(r.refusal(Errc::slot_range, ERR_SITE()));
    }
    if (bytes.size() != OsdSurface::kRowBytes) {
        return std::unexpected(OsdRowBytes::of(bytes).refusal(Errc::bad_format, ERR_SITE()));
    }

    const auto cmd = osd_word(OsdOp::Write, r.wire_word());
    if (auto e = frame.begin(sel(), cmd); !e)
        return std::unexpected(e.error());
    for (const std::uint8_t b : bytes) {
        if (auto e = frame.word(hal::LinkWord{b}); !e)
            return e;
    }
    return frame.end();
}

Ex<void> OsdTransport::set_visible(ILinkFrame& frame, bool visible) {
    return visible ? enable(frame, OsdMode::DisableKeyboard)
                   : command(frame, osd_word(OsdOp::Disable, hal::LinkWord{}));
}

Ex<void> OsdTransport::enable(ILinkFrame& frame, OsdMode mode) {

    const auto bits = hal::LinkWord{static_cast<std::uint16_t>(mode)};
    return command(frame, osd_word(OsdOp::Enable, bits.masked(kModeMask)));
}

Ex<void> OsdTransport::enable_info(ILinkFrame& frame, InfoWindow window) {

    if (auto e = frame.begin(sel(), osd_word(OsdOp::EnableInfo, hal::LinkWord{})); !e) {
        return std::unexpected(e.error());
    }
    for (const hal::LinkWord word : window.wire_words()) {
        if (auto e = frame.word(word); !e)
            return e;
    }
    return frame.end();
}

Ex<void> OsdTransport::set_rotation(ILinkFrame& frame, OsdRotation rotate) {

    if (auto e = frame.begin(sel(), osd_word(OsdOp::Disable, hal::LinkWord{})); !e) {
        return std::unexpected(e.error());
    }
    constexpr unsigned kDummyWords = 4;
    for (unsigned i = 0; i < kDummyWords; ++i) {
        if (auto e = frame.word(hal::LinkWord{}); !e)
            return e;
    }

    for (const hal::LinkWord word : rotation_words(rotate)) {
        if (auto e = frame.word(word); !e)
            return e;
    }
    return frame.end();
}

Ex<void> OsdTransport::menu_ctl(ILinkFrame& frame, bool enable_menu) {

    if (!enable_menu)
        return command(frame, osd_word(OsdOp::Disable, hal::LinkWord{}));
    if (auto e = command(frame, osd_word(OsdOp::WriteLatch, hal::LinkWord{})); !e)
        return e;
    return command(frame, osd_word(OsdOp::Enable, hal::LinkWord{}));
}

}
