// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/confine_under.h"

#include <optional>
#include <vector>

#include "svc/vfs.h"

namespace mister::svc {

namespace {

[[nodiscard]] std::unexpected<Error> refused() noexcept {
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

[[nodiscard]] std::optional<std::vector<std::string_view>> folded(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const std::size_t cut = path.find('/');
        const std::string_view part = path.substr(0, cut);
        path = cut == std::string_view::npos ? std::string_view{} : path.substr(cut + 1);
        if (part.empty() || part == ".")
            continue;
        if (part == "..") {
            if (parts.empty())
                return std::nullopt;
            parts.pop_back();
            continue;
        }
        parts.push_back(part);
    }
    return parts;
}

[[nodiscard]] bool inside(std::string_view root, std::string_view real) noexcept {
    if (real == root)
        return true;
    if (root == "/")
        return real.starts_with('/');
    return real.size() > root.size() && real.starts_with(root) && real[root.size()] == '/';
}

}

Ex<std::string> ConfineRoot::confine(const Vfs& vfs, std::string_view guest) const {
    const std::string_view root = path;

    if (root.empty())
        return refused();
    if (guest.empty() || guest.front() == '/')
        return refused();
    if (guest.find('\0') != std::string_view::npos)
        return refused();
    const auto parts = folded(guest);
    if (!parts)
        return refused();

    const auto real_root = vfs.canonical(root);
    if (!real_root)
        return refused();

    std::string host(root);
    std::string parent(root);
    for (const std::string_view part : *parts) {
        parent = host;
        if (host.empty() || host.back() != '/')
            host.push_back('/');
        host.append(part);
    }

    if (const auto real = vfs.canonical(host)) {
        if (!inside(*real_root, *real))
            return refused();
        return host;
    }

    if (vfs.name_kind(host) != Vfs::NameKind::Absent)
        return refused();
    const auto dir = vfs.canonical(parent);
    if (!dir || !inside(*real_root, *dir))
        return refused();
    return host;
}

}
