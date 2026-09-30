// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "app/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "infra/loan_channel.h"
#include "infra/counter.h"

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
        std::uint8_t scaled = 0;
        std::uint8_t ok = 0;
    };

    using Chan = xthread::LoanChannel<Shot, kDepth, SeatTag::Ui, SeatTag::Diag>;
    using Pending = Chan::Loan;
    using Job = Chan::Job;

    ScreenshotQueue() noexcept : chan_(xthread::Polled{}, xthread::Polled{}) {}

    [[nodiscard]] Pending arm() noexcept {
        auto p = chan_.acquire();
        if (!p) drops_.add(1);
        return p;
    }
    void submit(Pending&& p) noexcept { chan_.send(std::move(p)); }

    [[nodiscard]] Job take() noexcept { return chan_.take(); }

    [[nodiscard]] Pending reap() noexcept { return chan_.reap(); }

    std::uint32_t drops() const noexcept { return drops_.get(); }

    std::uint32_t result_drops() const noexcept { return chan_.census().breaches; }

private:
    Chan chan_;
    xthread::Counter drops_{};
};

inline std::vector<std::uint8_t> encode_png_rgb24(std::uint32_t w, std::uint32_t h,
                                                  std::span<const std::uint8_t> rgb) {
    auto crc_step = [](std::uint32_t crc, std::uint8_t byte) noexcept {
        crc ^= byte;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        return crc;
    };
    std::vector<std::uint8_t> out;
    if (w == 0 || h == 0 || rgb.size() != static_cast<std::size_t>(w) * h * 3) return out;

    const std::size_t row = static_cast<std::size_t>(w) * 3 + 1;
    const std::size_t raw_len = row * h;

    auto be32 = [&out](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v >> 24));
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v));
    };

    static constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    out.insert(out.end(), kSig, kSig + 8);

    be32(13);
    const std::size_t ihdr_at = out.size();
    const std::uint8_t ihdr_tail[5] = {8, 2, 0, 0, 0};
    out.insert(out.end(), {'I', 'H', 'D', 'R'});
    be32(w);
    be32(h);
    out.insert(out.end(), ihdr_tail, ihdr_tail + 5);
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = ihdr_at; i < out.size(); ++i)
        crc = crc_step(crc, out[i]);
    be32(~crc);

    const std::size_t blocks = (raw_len + 65534) / 65535;
    be32(static_cast<std::uint32_t>(2 + raw_len + blocks * 5 + 4));
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
            block_left = left < 65535 ? left : 65535;
            out.push_back(left <= 65535 ? 1 : 0);
            out.push_back(static_cast<std::uint8_t>(block_left & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(block_left >> 8));
            out.push_back(static_cast<std::uint8_t>(~block_left & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((~block_left >> 8) & 0xFFu));
        }
        out.push_back(v);
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
        ++emitted;
        --block_left;
    };
    for (std::uint32_t y = 0; y < h; ++y) {
        raw_byte(0);
        const std::size_t base = static_cast<std::size_t>(y) * w * 3;
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * 3; ++i)
            raw_byte(rgb[base + i]);
    }
    be32((b << 16) | a);
    crc = 0xFFFFFFFFu;
    for (std::size_t i = idat_at; i < out.size(); ++i)
        crc = crc_step(crc, out[i]);
    be32(~crc);

    be32(0);
    out.insert(out.end(), {'I', 'E', 'N', 'D'});
    be32(0xAE426082u);
    return out;
}

}  // namespace mister::app
