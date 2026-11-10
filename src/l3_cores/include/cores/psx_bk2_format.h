// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "cores/digest_value.h"
#include "cores/i_movie_format.h"
#include "cores/movie_facts.h"
#include "cores/movie_frame.h"
#include "cores/psx_movie_codec.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/track.h"

namespace mister::svc {
class IChdSource;
}

namespace mister::cores {

class PsxBk2Format final : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Layout = PsxMovieCodec::Layout;
    using Emu = PsxMovieCodec::Emu;
    using FirmwareRegion = PsxMovieCodec::FirmwareRegion;

    static constexpr std::string_view kPlatformName = "PSX";

    struct ColumnRole {
        static constexpr std::uint8_t kPadCount = PsxMovieCodec::kPadBitCount;
        enum class MovieCommand : std::uint8_t {
            Value = 0x40,
            Reset = 0x41,
            Power = 0x42,
            Tray = 0x43,
        };
        std::uint8_t v = 0;

        [[nodiscard]] static constexpr ColumnRole pad(JoyBit bit) noexcept {
            return ColumnRole{bit.v};
        }

        [[nodiscard]] static constexpr ColumnRole command(MovieCommand c) noexcept {
            return ColumnRole{static_cast<std::uint8_t>(c)};
        }

        [[nodiscard]] constexpr bool is_pad() const noexcept { return v < kPadCount; }

        [[nodiscard]] constexpr proto::JoyMask pad_mask() const noexcept {
            return JoyBit{v}.mask();
        }

        [[nodiscard]] constexpr MovieCommand command() const noexcept {
            return static_cast<MovieCommand>(v);
        }

        friend constexpr bool operator==(ColumnRole, ColumnRole) = default;
    };

    static constexpr std::size_t kMaxFiles = 99;

    static constexpr std::size_t kTocPrefixBytes = 4 * (3 + 3 * 100);

    static constexpr std::uint64_t kKeySectors = 26;
    static constexpr std::uint64_t kSectorBytes = svc::kRawFrameBytes;

    static constexpr std::size_t kChdFrameBytes = svc::kFullFrameBytes;
    static constexpr std::uint32_t kChdHunkMax = 1u << 20;

    [[nodiscard]] infra::OptRef<const MovieArchiveFormat> archive() const noexcept override;
    [[nodiscard]] bool starts_log(std::string_view line) const noexcept override;
    [[nodiscard]] bool ends_log(std::string_view line) const noexcept override;
    [[nodiscard]] Ex<MovieFacts> header_line(
        std::string_view line,
        const MovieFacts& so_far
    ) const noexcept override;
    [[nodiscard]] Ex<void> finish_header(const MovieFacts& f) const noexcept override;
    [[nodiscard]] Ex<MovieFrame> frame(
        std::string_view line,
        const MovieFacts& f
    ) const noexcept override;

    [[nodiscard]] ReplayLead default_lead(const MovieFacts&) const noexcept override { return {}; }

    [[nodiscard]] Ex<DigestSource> digest_source(
        const svc::Vfs& vfs,
        std::string_view rom
    ) const override;

    [[nodiscard]] static Ex<std::vector<std::uint8_t>> toc_prefix(
        std::string_view cue,
        std::span<const proto::FileSize> file_sizes
    );

    [[nodiscard]] static Ex<std::vector<std::uint8_t>> chd_key(svc::IChdSource& chd);

    [[nodiscard]] static Error chd_open_error(const Error& e) noexcept;
};

}
