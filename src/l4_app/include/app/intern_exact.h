// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

#include "app/link_tx_channel.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

[[nodiscard]] proto::TxSlabId intern_exact(
    const svc::Vfs& vfs,
    LinkTxChannel<SeatTag::Main>& inbox,
    std::string_view rel,
    std::size_t want
);

}
