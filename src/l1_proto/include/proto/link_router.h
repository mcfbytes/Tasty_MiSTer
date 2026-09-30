// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/link_event.h"
#include "proto/types.h"

namespace mister::proto {

class ILinkRouter {
public:
    [[nodiscard]] virtual bool push(const LinkEvent& e) noexcept = 0;
    [[nodiscard]] virtual bool can_intern(LinkEvent::Kind k, std::size_t bytes,
                                          unsigned chunks) const noexcept = 0;
    [[nodiscard]] virtual Ex<RxSlabId> intern(LinkEvent::Kind k,
                                              std::span<const std::uint8_t> bytes) noexcept = 0;
    static constexpr std::size_t kChunkBytes = 8192;

protected:
    constexpr ILinkRouter() noexcept = default;
    ~ILinkRouter() = default;
};

}  // namespace mister::proto
