// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/file_slots.h"

#include <array>
#include <charconv>
#include <limits>
#include <optional>

#include "infra/ascii.h"
#include "proto/item_rule.h"

#include "conf_str_detail.h"

namespace mister::proto {

IoIndex ConfStrEntry::default_index() const noexcept {
    if (kind != FileRowKind::File)
        return IoIndex{};

    std::uint8_t next = selentry;
    ++next;
    return IoIndex{next};
}

ConfStrFileRow ConfStrEntry::wire_row() const noexcept {
    return ConfStrFileRow{ext, ioctl_index, load_addr, opensave, addon, addon_after};
}

FileSlots FileSlots::of(const ItemTable& ast) {
    FileSlots slots;

    std::uint8_t selentry = 0;
    constexpr unsigned kLoadAddrSubfield = 3;
    const std::span<const Item> items = ast.items();
    for (std::size_t n = 0; n < items.size(); ++n) {
        const Item& it = items[n];
        if (evaluate(it, OsdMask{0}).hidden)
            continue;

        if (!it.declares_page && it.page != kRootSubmenu)
            continue;

        const Selectable sel = selectable_of(it.kind);
        if (sel == Selectable::Never)
            continue;

        if (const std::optional<FileRowKind> kind = file_row_kind_of(it.kind)) {
            ConfStrEntry fs{};
            fs.kind = *kind;
            fs.selentry = selentry;
            fs.digit = it.digit;
            fs.opensave = it.opensave;
            fs.store_name = it.store_name;

            if (const std::string_view ext = ast.subfield(it, 1); !ext.empty()) {
                fs.ext.assign(ext.data(), ext.size());
            }

            fs.ioctl_index = fs.digit ? fs.digit->io_index() : fs.default_index();
            if (fs.carries_load_addr())
                fs.load_addr = parse_load_addr(ast.subfield(it, kLoadAddrSubfield));
            const Item* addon = detail::bound_addon(items, &it);
            fs.addon = std::string{detail::addon_list(ast, addon)};
            fs.addon_after = detail::addon_after(ast, addon);
            slots.rows_.push_back(std::move(fs));
        }

        ++selentry;
    }
    return slots;
}

std::uint32_t parse_load_addr(std::string_view field) noexcept {
    constexpr int kHexRadix = 16;
    std::size_t i = 0;
    while (i < field.size() && (field[i] == ' ' || (field[i] >= '\t' && field[i] <= '\r')))
        ++i;
    bool negate = false;
    if (i < field.size() && (field[i] == '+' || field[i] == '-'))
        negate = field[i++] == '-';
    constexpr std::size_t kPrefixLen = 2;
    if (i + 1 < field.size() && field[i] == '0' && (field[i + 1] == 'x' || field[i + 1] == 'X'))
        i += kPrefixLen;
    std::uint32_t u = 0;
    const char* const first = field.data() + i;
    const auto [end, ec] = std::from_chars(first, field.data() + field.size(), u, kHexRadix);
    if (ec == std::errc::result_out_of_range)
        return std::numeric_limits<std::uint32_t>::max();
    if (end == first)
        return 0;
    return negate ? 0u - u : u;
}

FileSlotHit FileSlots::find(FileRowKind kind, FileSlotDigit digit) const noexcept {
    const ConfStrEntry* hit = nullptr;
    for (const ConfStrEntry& s : rows_) {

        if (s.kind == kind && FileSlotDigit::or_zero(s.digit) == digit)
            hit = &s;
    }
    if (hit != nullptr)
        return FileSlotHit{infra::opt_ref_of(hit), FileSlotMatch::Exact};

    for (const ConfStrEntry& s : rows_) {
        if (s.selentry == 0 && s.kind == kind)
            return FileSlotHit{infra::OptRef<const ConfStrEntry>{s}, FileSlotMatch::RowZero};
    }
    return FileSlotHit{};
}

FileSlotHit FileSlots::load(FileSlotDigit digit) const noexcept {
    return find(FileRowKind::File, digit);
}

std::uint8_t ext_subindex(std::string_view filename, std::string_view ext_list) noexcept {
    constexpr std::size_t kExtGroup = 3;
    const std::size_t dot = filename.rfind('.');
    if (dot == std::string_view::npos)
        return 0;

    std::array<char, kExtGroup> e{' ', ' ', ' '};
    for (std::size_t i = 0; i < kExtGroup && dot + 1 + i < filename.size(); ++i) {
        e[i] = filename[dot + 1 + i];
    }
    std::uint8_t idx = 0;
    std::string_view rest = ext_list;
    while (!rest.empty()) {
        bool found = true;
        for (std::size_t i = 0; i < kExtGroup; ++i) {
            const char c = i < rest.size() ? rest[i] : ' ';
            if (c == '*')
                break;
            if (c == '?')
                continue;
            if (infra::ascii_lower(c) != infra::ascii_lower(e[i])) {
                found = false;
                break;
            }
        }
        if (found)
            return idx;
        if (rest.size() <= kExtGroup)
            break;
        ++idx;
        rest.remove_prefix(kExtGroup);
    }
    return 0;
}

}
