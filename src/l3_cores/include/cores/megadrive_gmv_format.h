// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/digest_value.h"
#include "cores/megadrive_bk2_format.h"
#include "cores/movie_facts.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/file.h"

namespace mister::cores {

class MegaDriveGmvFormat final : public MegaDriveBk2Format {
    TASTY_SEAT_EXEMPT(boot);

public:
    static constexpr ReplayLead kGensLead{-2};

    [[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_movie(
        const svc::Vfs& vfs,
        std::string_view path
    ) const override;

    [[nodiscard]] bool rom_matches(
        const DigestValue& d,
        const MovieFacts& f
    ) const noexcept override;
};

[[nodiscard]] Ex<std::unique_ptr<svc::IFile>> open_gmv_text(std::unique_ptr<svc::IFile> gmv);

}
