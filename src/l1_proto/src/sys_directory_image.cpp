// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/sys_directory_image.h"

#include "proto/axi_block_decl.h"

namespace mister::proto {

SysDirectoryImage SysDirectoryImage::read(
    const hal::RegisterWindow<hal::SysDirectory>& dir
) noexcept {
    std::array<std::uint32_t, kWords> w{};
    for (std::size_t i = 0; i < kWords; ++i) {
        w[i] = dir.read(static_cast<hal::SysDirectory::Reg>(i * 4));
    }
    return SysDirectoryImage{w};
}

AxiBlockDecl SysDirectoryImage::entry(std::uint32_t i) const noexcept {
    AxiBlockDecl d{};
    d.kind = entry_word(i, EntryWord::Kind);
    d.version = entry_word(i, EntryWord::Version);
    d.offset = entry_word(i, EntryWord::Offset);
    d.size = entry_word(i, EntryWord::Size);
    for (std::uint32_t w = 0; w < d.ops.size(); ++w)
        d.ops[w] = entry_word(i, EntryWord::Ops, w);
    d.line = DeclaredLine::of(entry_word(i, EntryWord::Line));
    return d;
}

}
