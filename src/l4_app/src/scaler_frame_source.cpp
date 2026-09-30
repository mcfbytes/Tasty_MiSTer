// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scaler_frame_source.h"

#include <cstring>
#include <utility>

#include "hal/axi.h"
#include "hal/scaler_header.h"

namespace mister::app {

using hal::ScalerHeader;

ScalerFrameSource::ScalerFrameSource(hal::FpgaMemory window,
                                     const VideoGeometryCell& geometry) noexcept
    : window_(std::move(window)), geometry_(geometry) {}

std::uint32_t ScalerFrameSource::refusals() const noexcept {
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < kRefusalKinds; ++i)
        n += refused_[i];
    return n;
}

std::unexpected<Error> ScalerFrameSource::refuse(Refusal why, std::uint16_t site) noexcept {
    ++refused_[static_cast<std::size_t>(why)];
    return std::unexpected(Error{Errc::bad_format, site, static_cast<std::uint32_t>(why)});
}

Ex<IFrameSource::Frame> ScalerFrameSource::capture(bool scaled) {
    TASTY_SEAT_BODY(ScalerFrameSource);

    std::byte raw[ScalerHeader::kBytes]{};
    if (auto r = window_.read_at(0, std::span<std::byte>(raw)); !r)
        return refuse(Refusal::Aperture, ERR_SITE());
    const ScalerHeader head = ScalerHeader::decode(std::span<const std::byte, 16>(raw));
    if (!head.supported()) return refuse(Refusal::Unsupported, ERR_SITE());

    const std::uint32_t w = head.width;
    const std::uint32_t h = head.height;
    const std::size_t line = head.line;
    const std::size_t off = head.header_size;

    if (w == 0 || h == 0 || line < static_cast<std::size_t>(w) * 3u || off < ScalerHeader::kBytes)
        return refuse(Refusal::Corrupt, ERR_SITE());

    const std::size_t bytes = line * h;
    if (!hal::span_fits(off, bytes, window_.region().len))
        return refuse(Refusal::Oversize, ERR_SITE());

    if (static_cast<std::size_t>(w) * h * 4u > kStockCaptureBytes)
        return refuse(Refusal::Oversize, ERR_SITE());

    std::uint32_t out_w = w;
    std::uint32_t out_h = h;
    if (scaled) {
        out_w = head.output_width;
        out_h = head.output_height;

        if (const auto g = geometry_.sample(); g && g.value.valid && g.value.rotated)
            out_w = static_cast<std::uint32_t>(static_cast<float>(out_h) *
                                               (static_cast<float>(w) / static_cast<float>(h)));
        if (out_w == 0 || out_h == 0) return refuse(Refusal::Corrupt, ERR_SITE());
        if (out_w > kMaxScaledWidth || out_h > kMaxScaledHeight)
            return refuse(Refusal::Oversize, ERR_SITE());
    }

    const std::span<std::byte> image = window_.view(off, bytes);
    Frame frame;
    frame.width = out_w;
    frame.height = out_h;
    frame.rgb.resize(static_cast<std::size_t>(out_w) * out_h * 3u);

    if (!scaled) {
        for (std::uint32_t y = 0; y < h; ++y)
            std::memcpy(frame.rgb.data() + static_cast<std::size_t>(y) * w * 3u,
                        image.data() + static_cast<std::size_t>(y) * line,
                        static_cast<std::size_t>(w) * 3u);
    } else {
        for (std::uint32_t dy = 0; dy < out_h; ++dy) {
            const std::size_t sy = static_cast<std::size_t>(dy) * h / out_h;
            const std::byte* src = image.data() + sy * line;
            std::uint8_t* dst = frame.rgb.data() + static_cast<std::size_t>(dy) * out_w * 3u;
            for (std::uint32_t dx = 0; dx < out_w; ++dx) {
                const std::size_t sx = static_cast<std::size_t>(dx) * w / out_w;
                std::memcpy(dst + static_cast<std::size_t>(dx) * 3u, src + sx * 3u, 3u);
            }
        }
    }

    std::byte again[ScalerHeader::kBytes]{};
    if (auto r = window_.read_at(0, std::span<std::byte>(again)); r) {
        const ScalerHeader after = ScalerHeader::decode(std::span<const std::byte, 16>(again));
        if (after.frame_counter() != head.frame_counter()) ++torn_;
    }

    ++captures_;
    return frame;
}

}  // namespace mister::app
