// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/register_window.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/dir_entry_facts.h"
#include "proto/dir_version.h"
#include "proto/sys_dir_entry.h"

namespace mister::proto {

class SysDirectoryImage {
public:
    static constexpr std::size_t kWords = hal::SysDirectory::kSize / 4;

    [[nodiscard]] static SysDirectoryImage read(
        const hal::RegisterWindow<hal::SysDirectory>& dir
    ) noexcept;

    [[nodiscard]] static SysDirectoryImage of(
        const std::array<std::uint32_t, kWords>& words
    ) noexcept {
        return SysDirectoryImage{words};
    }

    [[nodiscard]] std::uint32_t magic() const noexcept { return words_[0]; }

    [[nodiscard]] DirVersion version() const noexcept { return DirVersion::of_wire(words_[1]); }

    [[nodiscard]] std::uint32_t count() const noexcept {
        return words_[2] < hal::SysDirectory::kMaxEntries ? words_[2]
                                                          : hal::SysDirectory::kMaxEntries;
    }

    [[nodiscard]] SysDirEntry entry(std::uint32_t i) const noexcept;

    [[nodiscard]] Ex<SysDirEntry> find(
        const DirEntryFacts& facts,
        hal::LwExtent window
    ) const noexcept;

private:
    enum class EntryWord : std::uint32_t { Kind, Version, Offset, Size, Ops, Line = 15 };

    [[nodiscard]] std::uint32_t entry_word(
        std::uint32_t i,
        EntryWord first,
        std::uint32_t w = 0
    ) const noexcept {
        return words_
            [(hal::SysDirectory::kEntryBase / 4)
             + (hal::SysDirectory::kEntryStride / 4) * i
             + std::to_underlying(first)
             + w];
    }

    explicit SysDirectoryImage(const std::array<std::uint32_t, kWords>& w) noexcept : words_(w) {}

    std::array<std::uint32_t, kWords> words_{};
};

}
