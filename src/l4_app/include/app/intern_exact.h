// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/link_tx_channel.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

template <std::size_t N>
    requires(N > 0 && N <= kLinkTxSlabBytes)
[[nodiscard]] std::optional<std::array<std::uint8_t, N>> read_exact(
    const svc::Vfs& vfs,
    std::string_view rel
) noexcept;

[[nodiscard]] proto::TxSlabId intern_exact(
    const svc::Vfs& vfs,
    LinkTxChannel<SeatTag::Main>& inbox,
    std::string_view rel,
    std::size_t want
);

}
