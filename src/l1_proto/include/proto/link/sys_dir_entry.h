// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "hal/sys_directory.h"
#include "hal/types.h"
#include "proto/dir_entry_kind.h"
#include "proto/link/dir_version.h"
#include "proto/link/dir_word.h"
#include "proto/link/entry_line.h"
#include "proto/op_bitmap.h"
#include "proto/types.h"

namespace mister::proto::link {

template <class Word>
class KindedDirEntry;

class SysDirEntry {
public:
    hal::SysDirectory::EntryIndex index{};
    std::optional<DirEntryKind> kind{};
    DirVersion version{};
    hal::LwOffset offset{};
    hal::LwExtent size{};
    hal::LwOffset cause{};
    EntryLine line{};
    CauseBits cause_bits{};
    EntryTag tag{};
    [[nodiscard]] OpBitmap bitmap() const noexcept;

private:
    friend class SysDirectoryImage;
    template <class W>
    friend class KindedDirEntry;
    friend struct SysDirEntryTestAccess;

    template <class Word>
    [[nodiscard]] DirWord word(Word w) const noexcept {
        if (std::to_underlying(w) >= kind_words_.size())
            return DirWord{};
        return kind_words_[std::to_underlying(w)];
    }

    std::array<DirWord, 24> kind_words_{};
};

}
