// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"

namespace mister::os {

Ex<bool> bluetooth_adapter_up();

bool hci_devlist_any_up(std::span<const std::byte> reply) noexcept;

}  // namespace mister::os
