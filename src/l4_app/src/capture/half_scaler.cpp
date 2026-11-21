// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/capture/half_scaler.h"
#include "infra/span_cut.h"

#include "app/picture_size.h"
#include "infra/byte_view.h"

namespace mister::app::capture {

RgbRows HalfScaler::picture(const RawFrameSlot::Frame& s) noexcept {
    TASTY_SEAT_BODY(HalfScaler);
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
    constexpr RowBytes kPixel{kRgbPixelBytes};
    auto* const out_bytes = infra::byte_cast<std::uint8_t>(dst).data();
    for_each_row(out.rows, [&](PictureRow y) {
        const auto* a = infra::byte_cast<const std::uint8_t>(
                            infra::affine::first(s.rgb.from_row(y.doubled()), src_row)
        )
                            .data();
        const auto* b = infra::byte_cast<const std::uint8_t>(
                            infra::affine::first(
                                s.rgb.from_row(y.doubled() + PictureRows{1}),
                                src_row
                            )
        )
                            .data();
        auto* const o = out_bytes + row_start(row, y);
        for (const RowByte x : row.indices()) {
            const RowByte c = x.paired_source(kPixel);
            const RowByte next = c + kPixel;
            x.element_at(o) = static_cast<std::uint8_t>(
                (kBoxRound
                 + c.element_at(a)
                 + next.element_at(a)
                 + c.element_at(b)
                 + next.element_at(b))
                >> kBoxShift
            );
        }
    });
    return RgbRows{dst, row, out.rows};
}

}
