// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bitstream_resolve.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <string>

#include "app/rbf_resolve.h"
#include "cores/registry.h"
#include "infra/ascii.h"
#include "infra/path_parts.h"
#include "svc/vfs.h"
#include "svc/xml_scan.h"

namespace mister::app {
namespace {

[[nodiscard]] std::string_view manifest_root_of(std::string_view rel) noexcept {
    const auto role = cores::manifest_doc_role();
    return role ? role->root_of(rel) : std::string_view{};
}

[[nodiscard]] Ex<std::string> read_manifest_at(
    const svc::Vfs& vfs,
    std::string_view rel,
    svc::OpenMode mode
) {
    auto mra = vfs.resolve(rel, svc::SearchPolicy{svc::search::kRootOnly, true});
    if (!mra)
        return std::unexpected(mra.error());
    auto f = vfs.open(*mra, mode);
    if (!f)
        return std::unexpected(f.error());
    const auto sz = (*f)->size();
    if (!sz)
        return std::unexpected(sz.error());
    const auto role = cores::manifest_doc_role();
    const std::uint64_t kDocMax = role ? role->doc_max() : 0;
    if (sz->v == 0 || sz->v > kDocMax) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), sz->detail()});
    }
    std::string doc(sz->file_word<std::size_t>(), '\0');
    std::size_t got = 0;
    while (got < doc.size()) {
        auto r = (*f)->read_at(got, std::as_writable_bytes(std::span<char>(doc).subspan(got)));
        if (!r)
            return std::unexpected(r.error());
        if (*r == 0)
            break;
        got += *r;
    }
    doc.resize(got);
    return doc;
}

void fill_facts(MraFacts& facts, std::string_view doc) {
    const auto role = cores::manifest_doc_role();
    if (!role)
        return;
    cores::ManifestDocFacts parsed = role->facts_of(doc);
    facts.vertical = parsed.vertical;
    facts.rotation = parsed.rotation;
    (void)facts.setname.assign(parsed.setname);
    facts.setname_same_dir = parsed.setname_same_dir;
}

}

bool same_image_name(std::string_view rel, std::string_view image) noexcept {
    return !rel.empty() && infra::ieq(infra::basename(rel), infra::basename(image));
}

bool names_front_end_image(std::string_view rel, XmlKind kind) noexcept {
    return kind == XmlKind::Rbf && infra::ieq(infra::basename(rel), kFrontEndImage);
}

[[nodiscard]] Ex<std::string> resolve_bitstream(
    const svc::Vfs& vfs,
    std::string_view rel,
    XmlKind kind
) {
    if (kind != XmlKind::Mra) {
        const svc::SearchPolicy policy = infra::ieq(infra::basename(rel), kFrontEndImage)
            ? svc::SearchPolicy{svc::search::kSdRootOnly, true}
            : svc::SearchPolicy{svc::search::kRootOnly, true};
        return vfs.resolve(rel, policy);
    }
    auto doc_r = read_manifest_at(vfs, rel, svc::OpenMode::ReadWhole);
    if (!doc_r)
        return std::unexpected(doc_r.error());
    const std::string_view frag = svc::xml::rbf_text(*doc_r);
    if (frag.empty())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    std::string dir(manifest_root_of(rel));
    if (!dir.empty())
        dir.push_back('/');
    dir.append("cores");
    auto name = resolve_rbf_name(vfs, dir, frag, true);
    if (!name)
        return std::unexpected(name.error());
    dir.push_back('/');
    dir.append(*name);
    return vfs.resolve(dir, svc::SearchPolicy{svc::search::kRootOnly, true});
}

bool validate_load(const svc::Vfs& vfs, const UiRequest::LoadCore& req) {
    if (req.path.empty())
        return true;
    auto p = resolve_bitstream(vfs, req.path.view(), req.xml);
    if (!p)
        return false;
    auto f = vfs.open(*p, svc::OpenMode::Read);
    return f.has_value();
}

MraFacts mra_facts_at(const svc::Vfs& vfs, std::string_view rel) {
    MraFacts facts{};
    facts.is_arcade = true;
    auto doc = read_manifest_at(vfs, rel, svc::OpenMode::ReadWhole);
    if (!doc)
        return facts;
    fill_facts(facts, *doc);
    return facts;
}

cores::ManifestDocFacts build_doc_facts(
    const svc::Vfs& vfs,
    std::string_view text,
    std::string_view path
) {
    const auto role = cores::manifest_doc_role();
    return role ? role->facts_at(text, path, vfs) : cores::ManifestDocFacts{};
}

}
