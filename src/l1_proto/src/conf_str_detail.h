// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "proto/item_table.h"

namespace mister::proto {

namespace {

struct Scan {
    std::uint32_t value;
    std::size_t pos;
};

inline std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

}

namespace detail {

inline SubmenuNo view_page(const Item& it) noexcept {
    return it.declares_page ? kRootSubmenu : it.page;
}

inline const Item* bound_addon(std::span<const Item> items, const Item* row) noexcept {
    const SubmenuNo page = view_page(*row);
    for (const Item* p = row; p != items.data();) {
        --p;
        const Item& it = *p;
        if (view_page(it) != page || evaluate(it, OsdMask{0}).hidden)
            continue;
        if (it.kind == ItemKind::Addon)
            return &it;
        if (it.kind == ItemKind::FileSlot || it.kind == ItemKind::MountSlot)
            return nullptr;
    }
    return nullptr;
}

inline std::string_view addon_list(const ItemTable& ast, const Item* addon) noexcept {
    if (addon == nullptr)
        return {};
    const std::string_view body = ast.text(addon->body);
    const std::size_t comma = body.find(',');
    return comma == std::string_view::npos ? std::string_view{} : body.substr(comma + 1);
}

inline bool addon_after(const ItemTable& ast, const Item* addon) noexcept {
    if (addon == nullptr)
        return false;
    constexpr std::size_t kAddonFlagIndex = 1;
    const std::string_view body = ast.text(addon->body);
    return body.size() > kAddonFlagIndex && body[kAddonFlagIndex] == '1';
}

}

}
