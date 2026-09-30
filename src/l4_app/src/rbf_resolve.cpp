// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rbf_resolve.h"

#include "svc/vfs.h"
#include "svc/xml_scan.h"

namespace mister::app {

namespace xml = mister::svc::xml;

namespace {

[[nodiscard]] bool stem_matches(std::string_view name, std::string_view prefix) noexcept {
    if (name.size() <= prefix.size()) return false;
    if (!xml::iequal(name.substr(0, prefix.size()), prefix)) return false;
    const char sep = name[prefix.size()];
    return sep == '.' || sep == '_';
}

}  // namespace

[[nodiscard]] Ex<std::string> resolve_rbf_name(const svc::Vfs& vfs, std::string_view dir,
                                               std::string_view stem, bool arcade) {
    if (stem.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    svc::ScanFilter filter{};
    filter.extensions = {};
    filter.directories = true;
    filter.files = true;
    filter.zip_as_directory = false;
    auto entries = vfs.scan(dir, filter);
    if (!entries) return std::unexpected(entries.error());

    std::string arcade_stem;
    if (arcade) {
        arcade_stem.reserve(7 + stem.size());
        arcade_stem.append("Arcade-");
        arcade_stem.append(stem);
    }

    std::string_view best;
    for (const svc::DirEntry& e : *entries) {
        if (e.is_dir) continue;
        if (e.is_zip_member) continue;
        const std::string_view name = e.name;
        if (name.size() <= 4) continue;
        if (!xml::iequal(name.substr(name.size() - 4), ".rbf")) continue;

        if (!stem_matches(name, stem) && !(arcade && stem_matches(name, arcade_stem))) continue;

        if (best.empty() || best.compare(name) < 0) best = name;
    }
    if (best.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    return std::string(best);
}

}  // namespace mister::app
