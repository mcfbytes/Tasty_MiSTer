// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>

#include "proto/osd_transport.h"
#include "hal/selected.h"

namespace mister::proto {

namespace {

constexpr std::uint16_t kCmdWrite = 0x20;
constexpr std::uint16_t kCmdDisable = 0x40;
constexpr std::uint16_t kCmdEnable = 0x41;
constexpr std::uint16_t kOsdInfoBit = 0x04;
constexpr std::uint16_t kRowMask = 0x1F;
constexpr std::uint16_t kModeMask = 0x0A;
constexpr std::uint16_t kMenuLatchRow = 8;

Ex<void> put(hal::ISpiTransport& link, std::uint16_t w) {
    auto r = link.transfer(hal::SpiWord{w});
    if (!r) return std::unexpected(r.error());
    return {};
}

}  // namespace

std::span<std::uint8_t> OsdSurface::row(OsdRow r) {
    if (r.v >= kMaxRows) {
        fatal(Error{Errc::slot_range, ERR_SITE(), r.v}, "osd row");
    }
    return std::span<std::uint8_t>(rows_[r.v], kRowBytes);
}

std::span<const std::uint8_t> OsdSurface::row(OsdRow r) const {
    if (r.v >= kMaxRows) {
        fatal(Error{Errc::slot_range, ERR_SITE(), r.v}, "osd row");
    }
    return std::span<const std::uint8_t>(rows_[r.v], kRowBytes);
}

void OsdSurface::publish(OsdRow r) noexcept {
    if (r.v >= kMaxRows) return;
    Row staged;
    std::memcpy(staged.b, rows_[r.v], kRowBytes);
    published_[r.v].publish(staged);
}

std::uint32_t OsdSurface::seq(OsdRow r) const noexcept {
    if (r.v >= kMaxRows) return 0;
    return published_[r.v].generation();
}

std::uint32_t OsdSurface::sample(OsdRow r, Row& dst) const noexcept {
    if (r.v >= kMaxRows) return 0;
    return published_[r.v].sample_into(dst);
}

std::uint32_t OsdSurface::refusals() const noexcept {
    std::uint32_t n = 0;
    for (const auto& cell : published_)
        n += cell.refusals();
    return n;
}

void OsdSurface::set_visible_rows(unsigned n) noexcept {
    visible_rows_.publish(VisibleRows{static_cast<std::uint8_t>(n > kMaxRows ? kMaxRows : n)});
}

unsigned OsdSurface::visible_rows() const noexcept {
    const auto s = visible_rows_.sample();
    return s ? static_cast<unsigned>(s.value.n) : kMaxRows;
}

void OsdSurface::clear() noexcept {
    for (unsigned i = 0; i < kPageRows; ++i) {
        for (unsigned b = 0; b < kRowBytes; ++b)
            rows_[i][b] = 0;
    }
    for (unsigned i = 0; i < kMaxRows; ++i) {
        publish(OsdRow{static_cast<std::uint8_t>(i)});
    }
}

hal::Selected OsdTransport::begin(hal::ISpiTransport& link) {
    hal::ChipSelect cs = hal::ChipSelect::Osd;
    switch (target_) {
        case OsdTarget::All:
            cs = hal::ChipSelect::Osd;
            break;
        case OsdTarget::Vga:
            cs = hal::ChipSelect::OsdVga;
            break;
        case OsdTarget::Hdmi:
            cs = hal::ChipSelect::OsdHdmi;
            break;
    }
    return hal::Selected(link, cs);
}

Ex<void> OsdTransport::command(hal::ISpiTransport& link, std::uint16_t cmd) {
    hal::Selected cs = begin(link);
    return put(link, cmd);
}

Ex<void> OsdTransport::flush_row(hal::ISpiTransport& link, OsdRow r,
                                 std::span<const std::uint8_t> bytes) {
    if (r.v >= OsdSurface::kMaxRows) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.v});
    }
    if (bytes.size() != OsdSurface::kRowBytes) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(bytes.size())});
    }
    hal::Selected cs = begin(link);
    const auto cmd = static_cast<std::uint16_t>(kCmdWrite | (r.v & kRowMask));
    if (auto e = put(link, cmd); !e) return e;
    for (const std::uint8_t b : bytes) {
        if (auto e = put(link, b); !e) return e;
    }
    return {};
}

Ex<void> OsdTransport::set_visible(hal::ISpiTransport& link, bool visible) {
    return visible ? enable(link, OsdMode::DisableKeyboard) : command(link, kCmdDisable);
}

Ex<void> OsdTransport::enable(hal::ISpiTransport& link, OsdMode mode) {

    const auto bits = static_cast<std::uint16_t>(mode);
    const auto m = static_cast<std::uint16_t>(bits & kModeMask);
    return command(link, static_cast<std::uint16_t>(kCmdEnable | m));
}

Ex<void> OsdTransport::enable_info(hal::ISpiTransport& link, std::uint16_t x, std::uint16_t y,
                                   std::uint16_t width, std::uint16_t height) {

    constexpr auto kCmdInfo = static_cast<std::uint16_t>(kCmdEnable | kOsdInfoBit);
    hal::Selected cs = begin(link);
    if (auto e = put(link, kCmdInfo); !e) return e;
    if (auto e = put(link, x); !e) return e;
    if (auto e = put(link, y); !e) return e;
    if (auto e = put(link, width); !e) return e;
    return put(link, height);
}

Ex<void> OsdTransport::set_rotation(hal::ISpiTransport& link, OsdRotation rotate) {

    hal::Selected cs = begin(link);
    if (auto e = put(link, kCmdDisable); !e) return e;
    for (int i = 0; i < 4; ++i) {
        if (auto e = put(link, 0); !e) return e;
    }
    return put(link, static_cast<std::uint16_t>(rotate));
}

Ex<void> OsdTransport::menu_ctl(hal::ISpiTransport& link, bool enable_menu) {

    if (!enable_menu) return command(link, kCmdDisable);
    constexpr auto kDoorbell = static_cast<std::uint16_t>(kCmdWrite | kMenuLatchRow);
    if (auto e = command(link, kDoorbell); !e) return e;
    return command(link, kCmdEnable);
}

}  // namespace mister::proto
