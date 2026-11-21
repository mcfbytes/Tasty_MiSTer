// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/tas/movie_rom_match.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "cores/tas/rom_digest.h"
#include "infra/ascii.h"
#include "svc/scan_filter.h"
#include "svc/vfs.h"

namespace mister::cores::tas {
namespace {

std::string rooted(const svc::Vfs& vfs, std::string_view rel) {
    std::string p = vfs.root_path();
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (p.empty())
        p = "/";
    if (p.back() != '/')
        p.push_back('/');
    p.append(rel);
    return p;
}

enum class RomConsider : std::int8_t { No, Match };

}

bool MovieRomMatch::NameBudget::take() noexcept {
    if (used_.reached(kCap))
        return false;
    used_ = used_.next();
    return true;
}

bool MovieRomMatch::ByteBudget::take(const IMovieFormat::DigestSource& src) noexcept {
    std::size_t prefix_bytes = src.prefix.size();
    for (const auto& a : src.alt_prefixes)
        prefix_bytes += a.size();
    const proto::FileSize prefix = proto::FileSize::of(infra::ByteCount{prefix_bytes});
    const proto::FileSize left = kCap - spent_;

    if (prefix > left || src.span.length > left - prefix)
        return false;
    spent_ += prefix + src.span.length;
    return true;
}

MovieRomMatch::Digested MovieRomMatch::digest(
    const IMovieFormat::DigestSource& src,
    DigestKind kind
) const {
    auto f = vfs_.open(src.file, svc::OpenMode::Read);
    if (!f)
        return {.fail = DigestFail::Open};
    return digest_file(**f, src, kind);
}

MovieRomMatch::Digested MovieRomMatch::digest_file(
    svc::IFile& file,
    const IMovieFormat::DigestSource& src,
    DigestKind kind
) {
    RomDigest dig{kind};
    std::vector<RomDigest> alts(src.alt_prefixes.size(), RomDigest{kind});
    if (!src.prefix.empty())
        dig.update(std::span<const std::uint8_t>(src.prefix.data(), src.prefix.size()));
    for (std::size_t i = 0; i < alts.size(); ++i)
        alts[i].update(std::span<const std::uint8_t>(src.alt_prefixes[i]));
    std::array<std::uint8_t, kDigestChunk.file_word<std::size_t>()> buf{};
    proto::FileOffset off = src.span.offset;
    proto::FileSize left = src.span.length;
    while (!left.empty()) {
        const auto got = file.read_at(
            off,
            std::as_writable_bytes(
                infra::affine::first(std::span<std::uint8_t>{buf}, kDigestChunk.min(left))
            )
        );
        if (!got || got->empty())
            return {.fail = got ? DigestFail::Short : DigestFail::Read};
        const std::span<const std::uint8_t> filled = got->first_in(
            std::span<const std::uint8_t>{buf}
        );
        dig.update(filled);
        for (auto& a : alts)
            a.update(filled);
        off += proto::FileSize::of(*got);
        left -= proto::FileSize::of(*got);
    }
    Digested out{.value = dig.finish()};
    for (auto& a : alts)
        out.alts.push_back(a.finish());
    return out;
}

bool MovieRomMatch::matches(const Digested& d, const MovieFacts& facts) const {
    if (d.value && format_.rom_matches(*d.value, facts))
        return true;
    for (const auto& a : d.alts) {
        if (format_.rom_matches(a, facts))
            return true;
    }
    return false;
}

MovieRomMatch::Search MovieRomMatch::find_rom(
    const MovieFacts& facts,
    std::string_view folder
) const {
    Search out{};
    if (!facts.digest) {
        out.end = Search::End::NoChecksum;
        return out;
    }
    const std::string rel = std::string(kGamesRel) + std::string(folder);
    const svc::ScanFilter filt{};
    NameBudget names;
    ByteBudget bytes;

    bool stopped = false;

    auto consider = [&](const std::string& child) -> RomConsider {
        auto src = format_.digest_source(vfs_, child);
        if (!src)
            return RomConsider::No;
        if (!bytes.take(*src)) {
            stopped = true;
            return RomConsider::No;
        }
        const auto d = digest(*src, facts.digest->kind);
        if (!matches(d, facts))
            return RomConsider::No;
        out.rom = child;
        return RomConsider::Match;
    };
    std::vector<std::string> subdirs;

    auto walk = [&](const std::string& dir, bool top) -> RomConsider {
        auto listing = vfs_.scan(dir, filt);
        if (!listing)
            return RomConsider::No;
        for (const svc::DirEntry& e : *listing) {
            if (e.name == "." || e.name == "..")
                continue;
            if (e.is_dir) {
                if (top)
                    subdirs.push_back(dir + "/" + e.name);
                continue;
            }
            if (!names.take()) {
                stopped = true;
                return RomConsider::No;
            }
            const RomConsider r = consider(rooted(vfs_, dir + "/" + e.name));
            if (r != RomConsider::No || stopped)
                return r;
        }
        return RomConsider::No;
    };
    RomConsider r = walk(rel, true);
    for (std::size_t i = 0; r == RomConsider::No && !stopped && i < subdirs.size(); ++i)
        r = walk(subdirs[i], false);
    out.names = names.taken();
    if (r == RomConsider::Match)
        out.end = Search::End::Found;
    else if (stopped)
        out.end = Search::End::Stopped;
    return out;
}

}
