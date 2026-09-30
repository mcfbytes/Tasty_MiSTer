// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"

namespace mister::svc {

struct ChdMetaRow;

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

}  // namespace mister::svc
