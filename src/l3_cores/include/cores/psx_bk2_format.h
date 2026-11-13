// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/digest_value.h"
#include "cores/i_movie_format.h"
#include "cores/movie_facts.h"
#include "cores/movie_frame.h"
#include "cores/psx_movie_codec.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

class PsxBk2Format final : public IMovieFormat {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Layout = PsxMovieCodec::Layout;
    using Emu = PsxMovieCodec::Emu;
    using FirmwareRegion = PsxMovieCodec::FirmwareRegion;

    static constexpr std::string_view kPlatformName = "PSX";

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
};

}
