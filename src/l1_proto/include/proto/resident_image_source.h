// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/image_source.h"
#include "proto/types.h"

namespace mister::proto {

class IResidentImageSource : public IImageSource {
public:
    [[nodiscard]] virtual Ex<std::size_t> read_at(SlotIndex slot, std::uint64_t offset,
                                                  std::span<std::uint8_t> dst) = 0;
    [[nodiscard]] virtual Ex<std::size_t> write_at(SlotIndex slot, std::uint64_t offset,
                                                   std::span<const std::uint8_t> src) = 0;

    [[nodiscard]] virtual Ex<FileSize> create(SlotIndex slot, std::span<const std::uint8_t>) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), slot.v});
    }

protected:
    IResidentImageSource() = default;
    IResidentImageSource(const IResidentImageSource&) = default;
    IResidentImageSource& operator=(const IResidentImageSource&) = default;
};

}  // namespace mister::proto
