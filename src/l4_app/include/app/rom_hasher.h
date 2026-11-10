// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "cores/movie_codec.h"
#include "cores/rom_digest.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class IFile;
}

namespace mister::app {

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
        cores::IMovieFormat::DigestSpan span,
        cores::RomDigest digest,
        std::vector<cores::RomDigest> alts = {}
    ) noexcept;

    [[nodiscard]] Pace step() noexcept;

    [[nodiscard]] cores::RomDigest& digest() noexcept { return digest_; }

    [[nodiscard]] std::vector<cores::RomDigest>& alts() noexcept { return alts_; }

private:
    std::unique_ptr<svc::IFile> file_{};

    proto::FileSize read_at_{};
    proto::FileSize remaining_{};
    cores::RomDigest digest_{};
    std::vector<cores::RomDigest> alts_{};
};

}
