// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/dir_count.h"
#include "hal/dir_layout.h"
#include "hal/reset_counts.h"
#include "proto/link/entry_line.h"
#include "proto/link/sys_directory_image.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "infra/prelude.h"
#include "proto/op_bitmap.h"

namespace mister::proto::link {

namespace {

using hal::SysDirectory;

constexpr std::array kHeaderRegs{
    SysDirectory::Reg::Magic,
    SysDirectory::Reg::Version,
    SysDirectory::Reg::Layout,
    SysDirectory::Reg::Count,
    SysDirectory::Reg::WinBase,
    SysDirectory::Reg::WinSize,
    SysDirectory::Reg::Enable,
    SysDirectory::Reg::Pending,
    SysDirectory::Reg::Owned,
    SysDirectory::Reg::Scratch,
    SysDirectory::Reg::Open,
    SysDirectory::Reg::LineEn,
    SysDirectory::Reg::Resets,
};

constexpr hal::LwOffset kOneWord{hal::LwOffset::kDword};

}

SysDirectoryImage SysDirectoryImage::read(const hal::RegisterWindow<SysDirectory>& dir) noexcept {
    std::array<hal::RegWord, kWords> w{};
    for (const SysDirectory::Reg r : kHeaderRegs)
        w[SysDirectory::dword_index(r)] = dir.read(r);

    const SysDirectoryImage head{w};
    const Ex<DirHeader> h = head.header_ok();
    if (!h)
        return head;
    const std::optional<hal::LwOffset> end = h->layout.table_end(h->count);
    if (!end)
        return head;
    for (hal::LwOffset at = h->layout.header().from_origin(); at < *end; at = at.plus(kOneWord))
        w[at.dword_index()] = dir.read(SysDirectory::reg_at(at));
    return SysDirectoryImage{w};
}

hal::RegWord SysDirectoryImage::header_word(SysDirectory::Reg r) const noexcept {
    return words_[SysDirectory::dword_index(r)];
}

DirHeader SysDirectoryImage::header() const noexcept {
    using Reg = SysDirectory::Reg;
    return DirHeader{
        .magic = header_word(Reg::Magic),
        .version = DirVersion::of_wire(header_word(Reg::Version)),
        .layout = hal::DirLayout::of(header_word(Reg::Layout)),
        .count = hal::DirCount::of(header_word(Reg::Count)),
        .win_base = hal::LwOffset::of(header_word(Reg::WinBase)),
        .win_size = hal::LwExtent::of(header_word(Reg::WinSize)),
        .resets = hal::ResetCounts::of(header_word(Reg::Resets))
    };
}

Ex<DirHeader> SysDirectoryImage::header_ok() const noexcept {
    const DirHeader h = header();
    if (h.magic != SysDirectory::kMagic) {
        return std::unexpected(h.magic.refusal(Errc::bad_format, ERR_SITE()));
    }
    if (!h.version.has_major(DirMajor::V2)) {
        return std::unexpected(h.version.refusal(Errc::negotiation, ERR_SITE()));
    }
    if (!h.layout.admits()) {
        return std::unexpected(h.layout.register_word().refusal(Errc::bad_format, ERR_SITE()));
    }
    if (!h.layout.fits(h.count)) {
        return std::unexpected(h.count.refusal(Errc::bad_format, ERR_SITE()));
    }
    return h;
}

Ex<DirHeader> SysDirectoryImage::valid_for(const AxiLwDecl& token) const noexcept {
    auto h = header_ok();
    if (!h)
        return h;
    if (h->win_base != token.offset) {
        return std::unexpected(h->win_base.refusal(Errc::aperture_range, ERR_SITE()));
    }
    if (h->win_size != token.size) {
        return std::unexpected(h->win_size.refusal(Errc::aperture_range, ERR_SITE()));
    }
    return h;
}

hal::RegWord SysDirectoryImage::word(
    SysDirectory::EntryIndex i,
    EntryWord at,
    std::uint32_t k
) const noexcept {
    const hal::DirLayout layout = header().layout;
    const hal::LwOffset rel{(std::to_underlying(at) + k) * hal::LwOffset::kDword};
    if (!layout.entry_stride().contains(rel))
        return hal::RegWord{};
    const hal::LwOffset pos = layout.entry_at(i).plus(rel);
    return pos.dword_index() < words_.size() ? words_[pos.dword_index()] : hal::RegWord{};
}

SysDirEntry SysDirectoryImage::entry(SysDirectory::EntryIndex i) const noexcept {
    SysDirEntry d{};
    d.index = i;
    const Ex<DirHeader> h = header_ok();
    if (!h || !h->count.covers(i))
        return d;
    d.kind = kind_of_wire(word(i, EntryWord::Kind).masked(SysDirectory::kKindMask));
    d.version = DirVersion::of_wire(word(i, EntryWord::Version));
    d.offset = hal::LwOffset::of(word(i, EntryWord::Offset));
    d.size = hal::LwExtent::of(word(i, EntryWord::Size));
    d.cause = hal::LwOffset::of(word(i, EntryWord::Cause));
    d.line = EntryLine::of(word(i, EntryWord::Line));
    d.cause_bits = CauseBits::of(word(i, EntryWord::CauseBits));
    d.tag = EntryTag::of(word(i, EntryWord::Tag));
    for (std::uint32_t k = 0; k < kKindWords; ++k)
        d.kind_words_[k] = DirWord::of(word(i, EntryWord::FirstKind, k));
    return d;
}

EntryLine SysDirectoryImage::walked_line(SysDirectory::EntryIndex i) const noexcept {
    const EntryLine mine = EntryLine::of(word(i, EntryWord::Line));
    if (!mine.declared() || !mine.framework())
        return EntryLine{};
    const hal::DirCount n = header().count;
    std::uint32_t named = 0;
    for (SysDirectory::EntryIndex j{}; n.covers(j); j = hal::next_entry(j)) {
        if (!word(j, EntryWord::Kind).intersects(SysDirectory::kKindMask))
            continue;
        if (EntryLine::of(word(j, EntryWord::Line)).names_same(mine))
            ++named;
    }
    return named > 1u ? EntryLine{} : mine;
}

Ex<SysDirEntry> SysDirectoryImage::find(
    const DirEntryFacts& facts,
    hal::LwExtent window
) const noexcept {
    auto h = header_ok();
    if (!h)
        return std::unexpected(h.error());
    for (SysDirectory::EntryIndex i{}; h->count.covers(i); i = hal::next_entry(i)) {
        SysDirEntry d = entry(i);
        if (d.kind != facts.kind)
            continue;
        if (!d.version.has_major(facts.major))
            continue;
        if (!d.offset.dword_aligned()
            || !d.offset.fits(window, d.size)
            || !d.size.covers(facts.block)) {
            continue;
        }
        if (d.offset < SysDirectory::kPageEnd)
            continue;
        if (d.cause != hal::LwOffset{}
            && (!d.cause.dword_aligned()
                || d.cause < d.offset
                || !d.size.contains(d.cause.relative_to(d.offset)))) {
            continue;
        }
        d.line = walked_line(i);
        return d;
    }
    return std::unexpected(h->count.refusal(Errc::not_found, ERR_SITE()));
}

OpBitmap SysDirEntry::bitmap() const noexcept {
    return DirWord::op_bitmap(std::span(kind_words_).first<OpBitmap::kWordCount>());
}

}
