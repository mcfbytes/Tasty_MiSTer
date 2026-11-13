// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"
#include "proto/conf_str_entry.h"

namespace mister::proto {

enum class FileSlotMatch : std::uint8_t { Exact, RowZero };

struct FileSlotHit {
    infra::OptRef<const ConfStrEntry> entry{};
    FileSlotMatch how = FileSlotMatch::Exact;
};

}
