// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/half_scaler.h"

#include "app/picture_size.h"
#include "infra/byte_view.h"

namespace mister::app {

RgbRows HalfScaler::picture(const RawFrameSlot::Frame& s) noexcept {
    TASTY_SEAT_BODY(HalfScaler);
    constexpr std::size_t kHalf = 2;
    constexpr unsigned kBoxPixels = 4;
    constexpr unsigned kBoxShift = 2;
    constexpr unsigned kBoxRound = kBoxPixels / 2;
    const PictureSize out = PictureSize{s.width, s.rgb.rows}.halved();
    if (out.empty())
        return {};
    const RowBytes row = rgb_row_of(out.width);
    const std::size_t bytes = rows_span(row, out.rows);
    if (arena_.slot_bytes() < bytes && !arena_.reserve(1, bytes))
        return {};
    const std::span<std::byte> dst = arena_.stripe(0).first(bytes);
    const RowBytes src_row = rgb_row_of(s.width);
    auto* const out_bytes = infra::byte_cast<std::uint8_t>(dst).data();
    for_each_row(out.rows, [&](PictureRow y) {
        const auto* a = infra::byte_cast<const std::uint8_t>(
                            src_row.head(s.rgb.from_row(y.doubled()))
        )
                            .data();
        const auto* b = infra::byte_cast<const std::uint8_t>(
                            src_row.head(s.rgb.from_row(y.doubled() + PictureRows{1}))
        )
                            .data();
        auto* const o = out_bytes + row_start(row, y);
        for (const std::size_t x : row.indices()) {
            const std::size_t c = (x / kRgbPixelBytes) * (kHalf * kRgbPixelBytes)
                + x % kRgbPixelBytes;
            const unsigned sum = kBoxRound
                + a[c]
                + a[c + kRgbPixelBytes]
                + b[c]
                + b[c + kRgbPixelBytes];
            o[x] = static_cast<std::uint8_t>(sum >> kBoxShift);
        }
    });
    return RgbRows{dst, row, out.rows};
}

}
