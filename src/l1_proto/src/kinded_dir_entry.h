// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "hal/types.h"
#include "infra/error.h"
#include "proto/declared_line.h"
#include "proto/dir_entry_facts.h"
#include "proto/op_bitmap.h"
#include "proto/sys_dir_entry.h"
#include "proto/sys_directory_image.h"
#include "dir_word.h"

namespace mister::proto {

template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr auto ops_word(E word) noexcept {
    return std::to_underlying(word);
}

enum class NoWords : std::size_t {};

template <class Word>
class KindedDirEntry;

template <class Word>
[[nodiscard]] Ex<KindedDirEntry<Word>> find_kinded(
    const SysDirectoryImage& dir,
    const DirEntryFacts& facts,
    hal::LwExtent window
) noexcept;

template <class Word>
class KindedDirEntry {
public:
    [[nodiscard]] hal::LwOffset offset() const noexcept { return raw_.offset; }

    [[nodiscard]] hal::LwExtent size() const noexcept { return raw_.size; }

    [[nodiscard]] DirVersion version() const noexcept { return raw_.version; }

    [[nodiscard]] DeclaredLine line() const noexcept { return raw_.line; }

    [[nodiscard]] OpBitmap bitmap() const noexcept { return raw_.bitmap(); }

    [[nodiscard]] DirWord word(Word w) const noexcept {
        const std::size_t i = std::to_underlying(w);
        return DirWord{i < raw_.ops_.size() ? raw_.ops_[i] : 0u};
    }

private:
    template <class W>
    friend Ex<KindedDirEntry<W>> find_kinded(
        const SysDirectoryImage&,
        const DirEntryFacts&,
        hal::LwExtent
    ) noexcept;

    explicit KindedDirEntry(const SysDirEntry& e) noexcept : raw_(e) {}

    SysDirEntry raw_;
};

template <class Word>
[[nodiscard]] Ex<KindedDirEntry<Word>> find_kinded(
    const SysDirectoryImage& dir,
    const DirEntryFacts& facts,
    hal::LwExtent window
) noexcept {
    auto e = dir.find(facts, window);
    if (!e)
        return std::unexpected(e.error());
    return KindedDirEntry<Word>{*e};
}

}
