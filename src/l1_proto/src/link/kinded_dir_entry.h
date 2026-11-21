// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/link/dir_entry_facts.h"
#include "proto/link/dir_word.h"
#include "proto/link/entry_line.h"
#include "proto/op_bitmap.h"
#include "proto/link/sys_dir_entry.h"
#include "proto/link/sys_directory_image.h"

namespace mister::proto::link {

template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr auto ops_word(E word) noexcept {
    return std::to_underlying(word);
}

enum class NoWords : std::size_t {};

template <class Word>
inline constexpr bool kHasOpBitmap = false;

template <>
inline constexpr bool kHasOpBitmap<NoWords> = true;

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
    [[nodiscard]] hal::SysDirectory::EntryIndex index() const noexcept { return raw_.index; }

    [[nodiscard]] hal::LwOffset offset() const noexcept { return raw_.offset; }

    [[nodiscard]] hal::LwExtent size() const noexcept { return raw_.size; }

    [[nodiscard]] DirVersion version() const noexcept { return raw_.version; }

    [[nodiscard]] hal::LwOffset cause() const noexcept { return raw_.cause; }

    [[nodiscard]] EntryLine line() const noexcept { return raw_.line; }

    [[nodiscard]] CauseBits cause_bits() const noexcept { return raw_.cause_bits; }

    [[nodiscard]] EntryTag tag() const noexcept { return raw_.tag; }

    [[nodiscard]] OpBitmap bitmap() const noexcept
        requires kHasOpBitmap<Word>
    {
        return raw_.bitmap();
    }

    [[nodiscard]] DirWord word(Word w) const noexcept { return raw_.word(w); }

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
concept CarriesOpBitmap = requires(const KindedDirEntry<Word>& e) { e.bitmap(); };

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
