// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "infra/error.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

Ex<void> durable_write(const svc::Vfs& vfs, std::string_view rel, std::span<const std::byte> bytes);

}
