// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "cores/tas/movie_codec.h"
#include "cores/tas/rom_digest.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class IFile;
}

namespace mister::app::replay {

class RomHasher {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Pace : std::uint8_t {
        Pending,
        Done,
        ReadFailed,
    };

    void arm(
        std::unique_ptr<svc::IFile> file,
        cores::tas::IMovieFormat::DigestSpan span,
        cores::tas::RomDigest digest,
        std::vector<cores::tas::RomDigest> alts = {}
    ) noexcept;

    [[nodiscard]] Pace step() noexcept;

    [[nodiscard]] cores::tas::RomDigest& digest() noexcept { return digest_; }

    [[nodiscard]] std::vector<cores::tas::RomDigest>& alts() noexcept { return alts_; }

private:
    std::unique_ptr<svc::IFile> file_{};

    proto::FileOffset read_at_{};
    proto::FileSize remaining_{};
    cores::tas::RomDigest digest_{};
    std::vector<cores::tas::RomDigest> alts_{};
};

}
