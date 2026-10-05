// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/frame_arena.h"
#include "app/frame_codec.h"
#include "infra/seat.h"

namespace mister::app {

class CscdCodec final : public IFrameCodec {
    TASTY_SEAT_RESIDENT(Encode);

public:
    static constexpr bool kAvailable = true;

    static constexpr std::size_t kHeadBytes = 2;
    static constexpr std::uint8_t kKeyBit = 0x01;

    [[nodiscard]] static constexpr std::size_t row_bytes(std::uint32_t w) noexcept {
        return (std::size_t{w} * 3u + 3u) & ~std::size_t{3};
    }
    [[nodiscard]] static constexpr std::size_t picture_bytes(std::uint32_t w,
                                                             std::uint32_t h) noexcept {
        return row_bytes(w) * h;
    }

    [[nodiscard]] static constexpr std::size_t payload_bound(std::uint32_t w,
                                                             std::uint32_t h) noexcept {
        const std::size_t n = picture_bytes(w, h);
        return kHeadBytes + n + n / 16u + 64u + 3u;
    }

    CscdCodec() noexcept = default;
    CscdCodec(const CscdCodec&) = delete;
    CscdCodec& operator=(const CscdCodec&) = delete;

    [[nodiscard]] std::size_t mapped_bytes() const noexcept { return arena_.bytes(); }

    void restart(const RecOptions&) noexcept override { TASTY_SEAT_BODY(CscdCodec); }
    void arm(const os::IClock*, std::int64_t) noexcept override { TASTY_SEAT_BODY(CscdCodec); }
    [[nodiscard]] Search search() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return {};
    }
    [[nodiscard]] Ex<void> begin(std::uint16_t w, std::uint16_t h) noexcept override;
    void end() noexcept override;
    [[nodiscard]] bool open() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return width_ != 0;
    }
    [[nodiscard]] std::uint16_t width() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return width_;
    }
    [[nodiscard]] std::uint16_t height() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return height_;
    }
    [[nodiscard]] std::size_t max_payload() const noexcept override {
        TASTY_SEAT_BODY(CscdCodec);
        return payload_bound(width_, height_);
    }

    [[nodiscard]] std::size_t encode(const std::byte* rgb, std::size_t line, bool key,
                                     std::span<std::byte> out) noexcept override;

    [[nodiscard]] std::size_t rekey(std::span<std::byte> out) noexcept override;

    [[nodiscard]] std::span<const std::byte> dup() noexcept override;

    static void half_scale(const std::byte* rgb, std::size_t line, std::uint16_t w, std::uint16_t h,
                           std::byte* dst) noexcept;

private:
    [[nodiscard]] std::size_t pack_(const std::byte* src, bool key,
                                    std::span<std::byte> out) noexcept;

    FrameArena arena_{};
    std::byte* prev_ = nullptr;
    std::byte* delta_ = nullptr;
    std::byte* work_ = nullptr;
    std::byte* dup_ = nullptr;
    std::size_t dup_len_ = 0;
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
};

}  // namespace mister::app
