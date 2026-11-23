// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cores/nvram_store.h"

namespace mister::cores::minimig {

enum class CdSave : std::uint8_t { Cd32Nvram, CdtvNvram, CdtvCard, kCount };

[[nodiscard]] SavePath cd_save_path(CdSave which, std::string_view disc_path) noexcept;

[[nodiscard]] SavePath cd32_shared_save_path() noexcept;

}
