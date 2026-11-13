// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/sys_directory_image.h"

#include <algorithm>
#include <utility>

#include "proto/op_bitmap.h"

namespace mister::proto {

namespace {

constexpr hal::LwOffset kDirectoryEnd = hal::LwExtent::of_layout<hal::SysDirectory>().from_origin();

}

SysDirectoryImage SysDirectoryImage::read(
    const hal::RegisterWindow<hal::SysDirectory>& dir
) noexcept {
    std::array<std::uint32_t, kWords> w{};
    for (std::size_t i = 0; i < kWords; ++i) {
        w[i] = dir.read(static_cast<hal::SysDirectory::Reg>(i * 4));
    }
    return SysDirectoryImage{w};
}

SysDirEntry SysDirectoryImage::entry(std::uint32_t i) const noexcept {
    SysDirEntry d{};
    d.kind = kind_of_wire(entry_word(i, EntryWord::Kind));
    d.version = DirVersion::of_wire(entry_word(i, EntryWord::Version));
    d.offset = hal::LwOffset{entry_word(i, EntryWord::Offset)};
    d.size = hal::LwExtent{entry_word(i, EntryWord::Size)};
    for (std::uint32_t w = 0; w < d.ops_.size(); ++w)
        d.ops_[w] = entry_word(i, EntryWord::Ops, w);
    d.line = DeclaredLine::of(entry_word(i, EntryWord::Line));
    return d;
}

OpBitmap SysDirEntry::bitmap() const noexcept {
    std::array<std::uint32_t, 8> w{};
    std::copy_n(ops_.begin(), w.size(), w.begin());
    return OpBitmap::of(w);
}

Ex<SysDirEntry> SysDirectoryImage::find(
    const DirEntryFacts& facts,
    hal::LwExtent window
) const noexcept {
    using hal::SysDirectory;
    const std::uint32_t magic = this->magic();
    if (magic != SysDirectory::kMagic) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), magic});
    }
    const DirVersion header_version = version();
    if (!header_version.has_major(DirMajor::V1)) {
        return std::unexpected(header_version.refused(Errc::negotiation, ERR_SITE()));
    }
    const std::uint32_t n = count();
    for (std::uint32_t i = 0; i < n; ++i) {
        const SysDirEntry d = entry(i);
        if (d.kind != facts.kind)
            continue;
        if (!d.version.has_major(facts.major)) {
            return std::unexpected(d.version.refused(Errc::negotiation, ERR_SITE()));
        }
        if (!d.offset.dword_aligned()
            || !d.size.covers(facts.block)
            || d.offset < kDirectoryEnd
            || !d.offset.fits(window, facts.block)) {
            return std::unexpected(d.offset.refused(Errc::aperture_range, ERR_SITE()));
        }
        return d;
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), n});
}

}
