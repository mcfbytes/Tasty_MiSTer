// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "cores/rom_digest.h"
#include "infra/seat.h"

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
        std::uint64_t off,
        std::uint64_t end,
        cores::RomDigest digest,
        std::vector<cores::RomDigest> alts = {}
    ) noexcept;

    [[nodiscard]] Pace step() noexcept;

    [[nodiscard]] cores::RomDigest& digest() noexcept { return digest_; }

    [[nodiscard]] std::vector<cores::RomDigest>& alts() noexcept { return alts_; }

private:
    std::unique_ptr<svc::IFile> file_{};
    std::uint64_t off_ = 0;
    std::uint64_t end_ = 0;
    cores::RomDigest digest_{};
    std::vector<cores::RomDigest> alts_{};
};

}
