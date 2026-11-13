// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "proto/conf_str_entry.h"
#include "proto/conf_str_file_row.h"
#include "proto/file_slot_hit.h"
#include "proto/item_table.h"
#include "proto/types.h"

namespace mister::proto {

class FileSlots {
public:
    [[nodiscard]] static FileSlots of(const ItemTable& ast);

    [[nodiscard]] std::span<const ConfStrEntry> rows() const noexcept { return rows_; }

    [[nodiscard]] FileSlotHit find(FileRowKind kind, FileSlotDigit digit) const noexcept;

    [[nodiscard]] FileSlotHit load(FileSlotDigit digit) const noexcept;

private:
    std::vector<ConfStrEntry> rows_;
};

}
