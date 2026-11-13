// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/frame_arena.h"
#include "app/frame_codec.h"
#include "app/picture_size.h"
#include "app/rec_options.h"
#include "app/row_bytes.h"
#include "infra/seat.h"

namespace mister::app {

class CscdCodec final : public IFrameCodec {
    TASTY_SEAT_RESIDENT(Encode);

public:
    static constexpr bool kAvailable = rec_codec_facts(RecCodec::Cscd).available;

    static constexpr std::size_t kHeadBytes = 2;
    static constexpr std::uint8_t kKeyBit = 0x01;

    static constexpr std::size_t kRowAlign = 4;

    [[nodiscard]] static constexpr std::size_t row_bytes(PicturePixels w) noexcept {
        return rows_span(w.row_bytes(PixelBytes::of(kRgbPixelBytes), RowAlign::of(kRowAlign)), 1);
    }

    [[nodiscard]] static constexpr std::size_t picture_bytes(PictureSize size) noexcept {
        return rows_span(RowBytes{row_bytes(size.width)}, size.rows);
    }

    static_assert(
        rec_codec_facts(RecCodec::Cscd).bytes_per_pixel == PixelBytes::of(kRgbPixelBytes)
    );
    static_assert(rec_codec_facts(RecCodec::Cscd).row_align == RowAlign::of(kRowAlign));

    static constexpr std::size_t kLzoOverheadDiv = 16;
    static constexpr std::size_t kLzoOverheadFloor = 64;
    static constexpr std::size_t kLzoOverheadTail = 3;

    [[nodiscard]] static constexpr std::size_t payload_bound(PictureSize size) noexcept {
        const std::size_t n = picture_bytes(size);
        return kHeadBytes + n + n / kLzoOverheadDiv + kLzoOverheadFloor + kLzoOverheadTail;
    }

    CscdCodec() noexcept = default;
    CscdCodec(const CscdCodec&) = delete;
    CscdCodec& operator=(const CscdCodec&) = delete;

    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return arena_.bytes(); }

    [[nodiscard]] Search restart(const RecOptions&) noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return {};
    }

    void end() noexcept override;

    [[nodiscard]] PictureSize size() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return size_;
    }

    [[nodiscard]] std::size_t max_payload() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return payload_bound(size_);
    }

    [[nodiscard]] Encoded encode(
        RgbRows rgb,
        bool key,
        std::span<std::byte> out,
        const FrameBudget& budget
    ) noexcept override;

    [[nodiscard]] std::span<const std::byte> rekey(std::span<std::byte> out) noexcept override;

    [[nodiscard]] std::span<const std::byte> dup() noexcept override;

private:
    [[nodiscard]] Ex<void> begin_(PictureSize size) noexcept override;

    [[nodiscard]] std::span<const std::byte> pack_(
        std::span<const std::byte> src,
        bool key,
        std::span<std::byte> out
    ) noexcept;

    FrameArena arena_{};
    std::span<std::byte> prev_{};
    std::span<std::byte> delta_{};
    std::span<std::byte> work_{};
    std::span<std::byte> dup_{};
    std::size_t dup_len_ = 0;
    PictureSize size_{};
};

}
