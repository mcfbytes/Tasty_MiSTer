// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/pending_load.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <span>
#include <string>
#include <utility>

#include "app/rbf_resolve.h"
#include "svc/vfs.h"
#include "svc/xml_scan.h"

namespace mister::app {
namespace {

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

[[nodiscard]] Ex<std::string> read_manifest_at(const svc::Vfs& vfs, std::string_view rel,
                                               svc::OpenMode mode) {
    auto mra = vfs.resolve(rel, svc::SearchPolicy{svc::search::kRootOnly, true});
    if (!mra) return std::unexpected(mra.error());
    auto f = vfs.open(*mra, mode);
    if (!f) return std::unexpected(f.error());
    const auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());
    constexpr std::uint64_t kDocMax = 0;
    if (sz->v == 0 || sz->v > kDocMax) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(sz->v)});
    }
    std::string doc(static_cast<std::size_t>(sz->v), '\0');
    std::size_t got = 0;
    while (got < doc.size()) {
        auto r = (*f)->read_at(got, std::as_writable_bytes(std::span<char>(doc).subspan(got)));
        if (!r) return std::unexpected(r.error());
        if (*r == 0) break;
        got += *r;
    }
    doc.resize(got);
    return doc;
}

void fill_facts(MraFacts& facts, std::string_view doc) {
    (void)facts;
    (void)doc;
}

}  // namespace

void PendingLoad::arm(const LoadRequest& req) noexcept {
    req_ = req;

    doc_.clear();
    doc_read_ = false;
}

Ex<std::string_view> PendingLoad::manifest_doc_(const svc::Vfs& vfs) const {
    if (doc_read_) return std::string_view{doc_};
    auto r = read_manifest_at(vfs, path(), svc::OpenMode::Read);
    if (!r) return std::unexpected(r.error());
    doc_ = std::move(*r);
    doc_read_ = true;
    return std::string_view{doc_};
}

bool same_image_name(std::string_view rel, std::string_view image) noexcept {
    const auto base = [](std::string_view p) noexcept {
        const std::size_t slash = p.rfind('/');
        return slash == std::string_view::npos ? p : p.substr(slash + 1);
    };
    return !rel.empty() && ieq(base(rel), base(image));
}

bool names_front_end_image(std::string_view rel, XmlKind kind) noexcept {
    if (kind != XmlKind::Rbf) return false;
    const std::size_t slash = rel.rfind('/');
    return ieq(slash == std::string_view::npos ? rel : rel.substr(slash + 1), kFrontEndImage);
}

[[nodiscard]] Ex<std::string> resolve_bitstream(const svc::Vfs& vfs, std::string_view rel,
                                                XmlKind kind) {
    if (kind != XmlKind::Mra) {
        const std::size_t slash = rel.rfind('/');
        const std::string_view base = slash == std::string_view::npos ? rel : rel.substr(slash + 1);
        const svc::SearchPolicy policy = ieq(base, kFrontEndImage)
                                             ? svc::SearchPolicy{svc::search::kSdRootOnly, true}
                                             : svc::SearchPolicy{svc::search::kRootOnly, true};
        return vfs.resolve(rel, policy);
    }
    auto doc_r = read_manifest_at(vfs, rel, svc::OpenMode::ReadWhole);
    if (!doc_r) return std::unexpected(doc_r.error());
    const std::string_view frag = svc::xml::rbf_text(*doc_r);
    if (frag.empty()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    const std::string_view arcade_root{};
    std::string dir(arcade_root);
    if (!dir.empty()) dir.push_back('/');
    dir.append("cores");
    auto name = resolve_rbf_name(vfs, dir, frag, true);
    if (!name) return std::unexpected(name.error());
    dir.push_back('/');
    dir.append(*name);
    return vfs.resolve(dir, svc::SearchPolicy{svc::search::kRootOnly, true});
}

Ex<std::string> PendingLoad::resolve(const svc::Vfs& vfs) const {
    if (req_.kind == XmlKind::Mra) return resolve_mra_(vfs);
    const std::string_view rel = path();
    const std::size_t slash = rel.rfind('/');
    const std::string_view base = slash == std::string_view::npos ? rel : rel.substr(slash + 1);
    const svc::SearchPolicy policy = ieq(base, kFrontEndImage)
                                         ? svc::SearchPolicy{svc::search::kSdRootOnly, true}
                                         : svc::SearchPolicy{svc::search::kRootOnly, true};
    return vfs.resolve(rel, policy);
}

MraFacts PendingLoad::mra_facts(const svc::Vfs& vfs) const {
    if (req_.kind != XmlKind::Mra) return MraFacts{};
    MraFacts facts{};
    facts.is_arcade = true;
    auto doc = manifest_doc_(vfs);
    if (!doc) return facts;
    fill_facts(facts, *doc);
    return facts;
}

MraFacts mra_facts_at(const svc::Vfs& vfs, std::string_view rel) {
    MraFacts facts{};
    facts.is_arcade = true;
    auto doc = read_manifest_at(vfs, rel, svc::OpenMode::ReadWhole);
    if (!doc) return facts;
    fill_facts(facts, *doc);
    return facts;
}

Ex<std::string> PendingLoad::resolve_mra_(const svc::Vfs& vfs) const {
    const std::string_view rel = path();
    auto doc_r = manifest_doc_(vfs);
    if (!doc_r) return std::unexpected(doc_r.error());
    const std::string_view doc = *doc_r;

    const std::string_view frag = svc::xml::rbf_text(doc);
    if (frag.empty()) {

        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    (void)rel;
    const std::string_view arcade_root{};
    std::string dir(arcade_root);
    if (!dir.empty()) dir.push_back('/');
    dir.append("cores");

    auto name = resolve_rbf_name(vfs, dir, frag, true);
    if (!name) return std::unexpected(name.error());
    dir.push_back('/');
    dir.append(*name);
    return vfs.resolve(dir, svc::SearchPolicy{svc::search::kRootOnly, true});
}

Ex<void> PendingLoad::validate(const svc::Vfs& vfs) const {
    if (req_.path.empty()) return {};
    auto p = resolve(vfs);
    if (!p) return std::unexpected(p.error());
    auto f = vfs.open(*p, svc::OpenMode::Read);
    if (!f) return std::unexpected(f.error());
    return {};
}

}  // namespace mister::app
