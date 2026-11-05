// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstring>

#include "proto/osd_transport.h"
#include "proto/osd_opcodes.h"

namespace mister::proto {

namespace {

constexpr std::uint16_t kRowMask = 0x1F;
constexpr std::uint16_t kModeMask = 0x0A;

}

std::span<std::uint8_t> OsdSurface::row(OsdRow r) {
    if (!kMaxRows.covers(r)) {
        fatal(Error{Errc::slot_range, ERR_SITE(), r.v}, "osd row");
    }
    return std::span<std::uint8_t>(row_cell(ui_.rows, r).data(), kRowBytes);
}

std::span<const std::uint8_t> OsdSurface::row(OsdRow r) const {
    if (!kMaxRows.covers(r)) {
        fatal(Error{Errc::slot_range, ERR_SITE(), r.v}, "osd row");
    }
    return std::span<const std::uint8_t>(row_cell(ui_.rows, r).data(), kRowBytes);
}

void OsdSurface::publish(OsdRow r) noexcept {
    if (!kMaxRows.covers(r))
        return;
    Row staged;
    std::memcpy(staged.b, row_cell(ui_.rows, r).data(), kRowBytes);
    row_cell(published_, r).publish(staged);
}

std::uint32_t OsdSurface::seq(OsdRow r) const noexcept {
    if (!kMaxRows.covers(r))
        return 0;
    return row_cell(published_, r).generation();
}

std::uint32_t OsdSurface::sample(OsdRow r, Row& dst) const noexcept {
    if (!kMaxRows.covers(r))
        return 0;
    return row_cell(published_, r).sample_into(dst);
}

std::uint32_t OsdSurface::refusals() const noexcept {
    std::uint32_t n = 0;
    for (const auto& cell : published_)
        n += cell.refusals();
    return n;
}

void OsdSurface::set_visible_rows(OsdRowCount n) noexcept {
    visible_rows_.publish(VisibleRows{std::min(n, kMaxRows)});
}

OsdRowCount OsdSurface::visible_rows() const noexcept {
    VisibleRows rows;
    if (visible_rows_.sample_into(rows) == 0)
        return kMaxRows;
    return rows.n;
}

void OsdSurface::clear() noexcept {
    constexpr std::size_t kPageBytes = kPageRows.size() * kRowBytes;
    std::memset(&ui_.rows, 0, kPageBytes);
    for (OsdRow r{}; kMaxRows.covers(r); r += kOneOsdRow)
        publish(r);
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
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(bytes.size())}
        );
    }
    const auto cmd = osd_word(OsdOp::Write, static_cast<std::uint16_t>(r.v & kRowMask));
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

Ex<void> OsdTransport::enable_info(
    proto::Link& link,
    std::uint16_t x,
    std::uint16_t y,
    std::uint16_t width,
    std::uint16_t height
) {

    if (auto e = link.command(chip_select(), osd_word(OsdOp::EnableInfo, 0)); !e) {
        return std::unexpected(e.error());
    }
    if (auto e = link.word(x); !e)
        return e;
    if (auto e = link.word(y); !e)
        return e;
    if (auto e = link.word(width); !e)
        return e;
    if (auto e = link.word(height); !e)
        return e;
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
    if (auto e = link.word(static_cast<std::uint16_t>(rotate)); !e)
        return e;
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
