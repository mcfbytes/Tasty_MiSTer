// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/movie_codec.h"
#include "infra/seat.h"

namespace mister::svc {
class IFile;
}

namespace mister::cores {

class MegaDriveMovieCodec final : public IMovieCodec {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr std::array<std::uint8_t, 12> kPadBit{3, 2, 1, 0, 4, 5, 6, 7, 9, 10, 11, 8};
    static constexpr std::string_view kPadMnemonic = "UDLRABCSXYZM";
    static constexpr std::array<std::string_view, 12> kPadName{
        "Up", "Down", "Left", "Right", "A", "B", "C", "Start", "X", "Y", "Z", "Mode"};

    enum class Pad : std::uint8_t { None, Three, Six };
    enum class Sys : std::uint8_t { None, First, Last };
    struct Layout {
        Pad p1 = Pad::None;
        Pad p2 = Pad::None;
        Sys sys = Sys::None;
        bool reset_first = false;
        bool gmv = false;
        std::uint8_t region = 0;
        bool pal_key = false;
        bool platform = false;
        bool core = false;
        [[nodiscard]] static Layout of(std::uint16_t bits) noexcept;
        [[nodiscard]] std::uint16_t bits() const noexcept;
    };

    [[nodiscard]] const MovieArchiveFormat* archive() const noexcept override;
    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie(const svc::Vfs& vfs,
                                                             std::string_view path) const override;
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<Facts> header_line(std::string_view line,
                                        const Facts& so_far) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const Facts& f) const noexcept override;
    [[nodiscard]] Ex<Frame> frame(std::string_view line, const Facts& f) const noexcept override;
    [[nodiscard]] SettingNeeds setting_needs(const Facts& f) const noexcept override;
    [[nodiscard]] Raster raster(const Facts& f) const noexcept override;
    [[nodiscard]] PowerOn power_on(const Facts& f) const noexcept override;

    static constexpr std::int32_t kGensLead = -2;
    static constexpr std::int32_t kGpgxLead = -3;
    [[nodiscard]] std::int32_t default_lead(const Facts& f) const noexcept override;

    [[nodiscard]] std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head, std::uint64_t size) const noexcept override;
    [[nodiscard]] bool rom_matches(const DigestValue& d, const Facts& f) const noexcept override;

    [[nodiscard]] std::uint8_t rom_digit() const noexcept override { return 1; }
};

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_gmv_text(std::unique_ptr<svc::IFile> gmv);

consteval bool megadrive_pad_bits_are_a_permutation() {
    unsigned seen = 0;
    for (const std::uint8_t b : MegaDriveMovieCodec::kPadBit) {
        if (b > 11) return false;
        seen |= 1u << b;
    }
    return seen == 0xFFFu;
}
static_assert(megadrive_pad_bits_are_a_permutation(),
              "the BK2 Genesis pad map is a permutation of JoyMask bits 0-11");
static_assert(MegaDriveMovieCodec::kPadMnemonic.size() == MegaDriveMovieCodec::kPadBit.size());

}  // namespace mister::cores
