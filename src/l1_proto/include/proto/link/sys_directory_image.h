// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/dir_count.h"
#include "hal/dir_layout.h"
#include "hal/register_window.h"
#include "hal/reset_counts.h"
#include "hal/sys_directory.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/axi_lw_decl.h"
#include "proto/link/dir_entry_facts.h"
#include "proto/link/dir_version.h"
#include "proto/link/entry_line.h"
#include "proto/link/sys_dir_entry.h"

namespace mister::proto::link {

struct DirHeader {
    hal::RegWord magic{};
    DirVersion version{};
    hal::DirLayout layout{};
    hal::DirCount count{};
    hal::LwOffset win_base{};
    hal::LwExtent win_size{};
    hal::ResetCounts resets{};

    [[nodiscard]] constexpr AxiLwDecl window() const noexcept { return {win_base, win_size}; }
};

class SysDirectoryImage {
public:
    static constexpr std::size_t kWords = hal::SysDirectory::kSize / 4;

    [[nodiscard]] static SysDirectoryImage read(
        const hal::RegisterWindow<hal::SysDirectory>& dir
    ) noexcept;

    [[nodiscard]] static SysDirectoryImage of(
        const std::array<std::uint32_t, kWords>& words
    ) noexcept {
        std::array<hal::RegWord, kWords> w{};
        for (std::size_t i = 0; i < kWords; ++i)
            w[i] = hal::RegWord{words[i]};
        return SysDirectoryImage{w};
    }

    [[nodiscard]] DirHeader header() const noexcept;

    [[nodiscard]] Ex<DirHeader> valid_for(const AxiLwDecl& token) const noexcept;

    [[nodiscard]] SysDirEntry entry(hal::SysDirectory::EntryIndex i) const noexcept;

    [[nodiscard]] Ex<SysDirEntry> find(
        const DirEntryFacts& facts,
        hal::LwExtent window
    ) const noexcept;

private:
    enum class EntryWord : std::uint32_t {
        Kind,
        Version,
        Offset,
        Size,
        Cause,
        Line,
        CauseBits,
        Tag,
        FirstKind = 8
    };
    static constexpr std::uint32_t kKindWords = 24;

    explicit SysDirectoryImage(const std::array<hal::RegWord, kWords>& w) noexcept : words_(w) {}

    [[nodiscard]] Ex<DirHeader> header_ok() const noexcept;
    [[nodiscard]] hal::RegWord header_word(hal::SysDirectory::Reg r) const noexcept;

    [[nodiscard]] hal::RegWord word(
        hal::SysDirectory::EntryIndex i,
        EntryWord at,
        std::uint32_t k = 0
    ) const noexcept;

    [[nodiscard]] EntryLine walked_line(hal::SysDirectory::EntryIndex i) const noexcept;
    std::array<hal::RegWord, kWords> words_{};
};

}
