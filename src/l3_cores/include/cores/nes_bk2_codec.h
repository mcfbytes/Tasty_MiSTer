// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_archive.h"
#include "cores/nes_movie_codec.h"
#include "infra/seat.h"

namespace mister::cores {

class NesBk2Codec final : public NesMovieCodec {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::uint8_t kColReset = 1;
    static constexpr std::uint8_t kColPower = 2;
    static constexpr std::uint8_t kColPad = 0x80;
    static constexpr std::uint8_t kColGroupEnd = 0xFF;

    static constexpr std::uint8_t kMovieVersion = 0x01;
    static constexpr std::uint8_t kPlatform = 0x02;
    static constexpr std::uint8_t kLogKey = 0x04;
    static constexpr std::uint8_t kSha1 = 0x08;

    static constexpr std::int32_t kNesHawkLead = -2;
    static constexpr std::int32_t kLeadMin = -8;
    static constexpr std::int32_t kLeadMax = 64;

    [[nodiscard]] const MovieArchiveFormat* archive() const noexcept override {
        return &kBk2Archive;
    }
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<Facts> header_line(std::string_view line,
                                        const Facts& so_far) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const Facts& f) const noexcept override;
    [[nodiscard]] Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept override;
    [[nodiscard]] std::int32_t default_lead(const Facts&) const noexcept override {
        return kNesHawkLead;
    }
    [[nodiscard]] LeadRange lead_range() const noexcept override { return {kLeadMin, kLeadMax}; }

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept override;

    [[nodiscard]] Ex<DigestSource> digest_source(const svc::Vfs& vfs,
                                                 std::string_view rom) const override;

    struct PadName {
        std::string_view name;
        std::uint8_t bit;
    };
    static constexpr std::array<PadName, 8> kPadNames{{{"Up", 3},
                                                       {"Down", 2},
                                                       {"Left", 1},
                                                       {"Right", 0},
                                                       {"Start", 7},
                                                       {"Select", 6},
                                                       {"B", 5},
                                                       {"A", 4}}};

    [[nodiscard]] static Ex<std::uint8_t> column_of(std::string_view name) noexcept;
};

consteval bool nes_bk2_pad_bits_are_a_permutation() {
    unsigned seen = 0;
    for (const NesBk2Codec::PadName& p : NesBk2Codec::kPadNames) {
        if (p.bit > 7) return false;
        if ((seen & (1u << p.bit)) != 0) return false;
        seen |= 1u << p.bit;
    }
    return seen == 0xFFu;
}
static_assert(nes_bk2_pad_bits_are_a_permutation(),
              "BizHawk's NES pad names are a permutation of bits 0-7");

}  // namespace mister::cores
