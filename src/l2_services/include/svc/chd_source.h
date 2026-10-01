// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "infra/error.h"

namespace mister::svc {

struct ChdMetaRow;
class IFile;

class IChdSource {
public:
    virtual ~IChdSource() = default;

    struct Geometry {
        std::uint32_t hunk_bytes = 0;
        std::uint32_t unit_bytes = 0;
        std::uint32_t hunk_count = 0;
    };
    virtual Geometry geometry() const noexcept = 0;

    virtual std::optional<ChdMetaRow> track_metadata(std::uint32_t index) = 0;

    virtual Ex<void> read_hunk(std::uint32_t hunk, std::span<std::byte> dst) = 0;

protected:
    IChdSource() = default;
    IChdSource(const IChdSource&) = default;
    IChdSource& operator=(const IChdSource&) = default;
};

struct ChdTrack {
    std::uint32_t number = 0;
    std::uint32_t frames = 0;
    std::uint32_t pregap = 0;
    std::uint32_t postgap = 0;
    char type[32]{};
    char subtype[32]{};
    char pgtype[32]{};
};

[[nodiscard]] std::optional<ChdTrack> parse_chd_track(const ChdMetaRow& row) noexcept;

[[nodiscard]] Ex<std::unique_ptr<IChdSource>> open_chd(std::unique_ptr<IFile> file);

}  // namespace mister::svc
