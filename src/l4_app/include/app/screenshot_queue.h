// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "app/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/loan_channel.h"
#include "infra/counter.h"
#include "svc/types.h"

namespace mister::app {

class ScreenshotQueue {
    TASTY_SEAT_MEDIATOR(Ui, Diag);

public:
    static constexpr std::size_t kDepth = 4;
    static constexpr std::size_t kNameMax = 256;

    struct Shot {
        TASTY_SEAT_MEDIATOR(Any, Any);

        FixedStr<kNameMax, StrFit::Clip> path{};
        CorrelationTag tag{};
        bool scaled = false;
        bool ok = false;
    };

    using Chan = xthread::LoanChannel<Shot, kDepth, SeatTag::Ui, SeatTag::Diag>;
    using Pending = Chan::Loan;
    using Job = Chan::Job;

    ScreenshotQueue() noexcept
        : chan_(xthread::Polled{}, xthread::Polled{}),
          ui_(chan_.owner()),
          diag_(chan_.worker()) {}

    [[nodiscard]] Pending arm() noexcept {
        auto p = ui_.acquire();
        if (!p)
            drops_.add(1);
        return p;
    }

    void submit(Pending&& p) noexcept { ui_.send(std::move(p)); }

    [[nodiscard]] Job take() noexcept { return diag_.take(); }

    [[nodiscard]] Pending reap() noexcept { return ui_.reap(); }

    std::uint32_t drops() const noexcept { return drops_.get(); }

    std::uint32_t result_drops() const noexcept { return chan_.breaches(); }

private:
    Chan chan_;
    Chan::Owner ui_;
    Chan::Worker diag_;
    xthread::Counter drops_{};
};

inline std::vector<std::uint8_t> encode_png_rgb24(
    std::uint32_t w,
    std::uint32_t h,
    std::span<const std::uint8_t> rgb
) {
    static_assert(std::endian::native == std::endian::little);
    constexpr std::size_t kChannels = 3;
    constexpr std::size_t kStoredBlockMax = 65535;
    constexpr std::size_t kStoredHeaderBytes = 5;
    constexpr std::size_t kZlibHeaderBytes = 2;
    constexpr std::size_t kAdlerBytes = 4;
    constexpr std::uint32_t kAdlerMod = 65521;
    constexpr std::uint32_t kIendCrc = 0xAE426082u;
    constexpr std::uint32_t kIhdrLength = 13;
    constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    constexpr std::array<std::uint8_t, 5> kIhdrTail{8, 2, 0, 0, 0};

    std::vector<std::uint8_t> out;
    const std::size_t row_pixels = std::size_t{w} * kChannels;
    if (w == 0 || h == 0 || rgb.size() != row_pixels * h)
        return out;

    const std::size_t row = row_pixels + 1;
    const std::size_t raw_len = row * h;

    auto be32 = [&out](std::uint32_t v) {
        const auto le = std::bit_cast<std::array<std::uint8_t, 4>>(v);
        out.insert(out.end(), le.rbegin(), le.rend());
    };

    auto be32_crc = [&out](std::span<const std::uint8_t> chunk) {
        const auto be = svc::crc32_extend({}, chunk).to_be_bytes();
        out.insert(out.end(), be.begin(), be.end());
    };

    out.insert(out.end(), kSignature.begin(), kSignature.end());

    be32(kIhdrLength);
    const std::size_t ihdr_at = out.size();
    out.insert(out.end(), {'I', 'H', 'D', 'R'});
    be32(w);
    be32(h);
    out.insert(out.end(), kIhdrTail.begin(), kIhdrTail.end());
    be32_crc(std::span<const std::uint8_t>(out).subspan(ihdr_at));

    const std::size_t blocks = (raw_len + kStoredBlockMax - 1) / kStoredBlockMax;
    be32(
        static_cast<std::uint32_t>(
            kZlibHeaderBytes + raw_len + blocks * kStoredHeaderBytes + kAdlerBytes
        )
    );
    const std::size_t idat_at = out.size();
    out.insert(out.end(), {'I', 'D', 'A', 'T'});
    out.push_back(0x78);
    out.push_back(0x01);

    std::uint32_t a = 1, b = 0;
    std::size_t emitted = 0;
    std::size_t block_left = 0;
    auto raw_byte = [&](std::uint8_t v) {
        if (block_left == 0) {
            const std::size_t left = raw_len - emitted;
            block_left = left < kStoredBlockMax ? left : kStoredBlockMax;
            out.push_back(left <= kStoredBlockMax ? 1 : 0);
            const auto len = static_cast<std::uint16_t>(block_left);
            const auto nlen = static_cast<std::uint16_t>(~len);
            const auto len_le = std::bit_cast<std::array<std::uint8_t, 2>>(len);
            const auto nlen_le = std::bit_cast<std::array<std::uint8_t, 2>>(nlen);
            out.insert(out.end(), len_le.begin(), len_le.end());
            out.insert(out.end(), nlen_le.begin(), nlen_le.end());
        }
        out.push_back(v);
        a = (a + v) % kAdlerMod;
        b = (b + a) % kAdlerMod;
        ++emitted;
        --block_left;
    };
    for (std::uint32_t y = 0; y < h; ++y) {
        raw_byte(0);
        const std::size_t base = std::size_t{y} * row_pixels;
        for (std::size_t i = 0; i < row_pixels; ++i)
            raw_byte(rgb[base + i]);
    }
    be32((b << std::numeric_limits<std::uint16_t>::digits) | a);
    be32_crc(std::span<const std::uint8_t>(out).subspan(idat_at));

    be32(0);
    out.insert(out.end(), {'I', 'E', 'N', 'D'});
    be32(kIendCrc);
    return out;
}

}
