// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "hal/types.h"
#include "proto/declared_line.h"
#include "proto/dir_entry_kind.h"
#include "proto/dir_version.h"
#include "proto/op_bitmap.h"

namespace mister::proto {

class SysDirEntry {
public:
    std::optional<DirEntryKind> kind{};
    DirVersion version{};
    hal::LwOffset offset{};
    hal::LwExtent size{};
    DeclaredLine line{};
    [[nodiscard]] OpBitmap bitmap() const noexcept;

private:
    friend class SysDirectoryImage;
    template <class Word>
    friend class KindedDirEntry;
    std::array<std::uint32_t, 12> ops_{};
};

}
