// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/conf_str.h"
#include "proto/item_rule.h"
#include "conf_str_detail.h"

#include <array>
#include <limits>
#include <optional>

#include "infra/ascii.h"

namespace mister::proto {

Ex<ConfStr> ConfStr::parse(std::string_view raw, hal::PhysRegion aperture) {
    ConfStr c;
    const auto fields = split(raw, ';');
    if (fields.empty()) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    c.name_ = std::string(fields[0]);

    constexpr std::size_t kCapsField = 1;
    constexpr std::size_t kFirstItemField = 2;

    if (fields.size() > kCapsField) {
        const std::string_view f1 = fields[1];
        std::size_t pos = 0;
        while (pos < f1.size()) {
            const std::size_t seg_start = pos;

            std::string_view rest = f1.substr(pos);
            rest = c.caps_.try_ss(rest, aperture);
            rest = c.caps_.try_uart(rest);
            rest = c.caps_.try_midi(rest);
            const auto irq = c.caps_.try_irq(rest);
            if (!irq)
                return std::unexpected(irq.error());
            const auto axi = c.caps_.try_axi(*irq);
            if (!axi)
                return std::unexpected(axi.error());
            pos = f1.size() - axi->size();

            const std::size_t comma = f1.find(',', pos);
            if (pos == seg_start) {

                const std::size_t unk_end = comma == std::string_view::npos ? f1.size() : comma;
                if (unk_end > seg_start) {
                    c.caps_.carry_unknown(f1.substr(seg_start, unk_end - seg_start));
                }
            }

            pos = comma == std::string_view::npos ? f1.size() : comma + 1;
        }
    }

    for (std::size_t i = kFirstItemField; i < fields.size(); ++i) {
        c.items_.emplace_back(fields[i]);
    }
    auto ast = ItemTable::parse(raw);
    if (!ast)
        return std::unexpected(ast.error());
    c.ast_ = std::move(*ast);
    c.walk_items();
    return c;
}

std::string_view ConfStr::button_list(int type) const noexcept {

    constexpr int kJnType = 1;
    constexpr int kJpType = 2;
    for (const std::string& raw : items_) {
        const std::string_view p{raw};

        if (p.empty())
            break;

        bool hit = false;
        if (type == 0) {
            hit = (p[0] == 'J');
        } else if (p.size() > 1 && p[0] == 'j') {
            hit = (type == kJnType && p[1] == 'n') || (type == kJpType && p[1] == 'p');
        }
        if (!hit)
            continue;

        const std::size_t comma = p.find(',');
        if (comma == std::string_view::npos)
            break;
        const std::string_view tail = p.substr(comma + 1);
        if (tail.empty())
            break;
        return tail;
    }
    return {};
}

namespace {

SubmenuNo view_page(const Item& it) noexcept {
    return it.declares_page ? kRootSubmenu : it.page;
}

const Item* bound_addon(std::span<const Item> items, std::size_t row) noexcept {
    const SubmenuNo page = view_page(items[row]);
    for (std::size_t i = row; i-- > 0;) {
        const Item& it = items[i];
        if (view_page(it) != page || evaluate(it, OsdMask{0}).hidden)
            continue;
        if (it.kind == ItemKind::Addon)
            return &it;
        if (it.kind == ItemKind::FileSlot || it.kind == ItemKind::MountSlot)
            return nullptr;
    }
    return nullptr;
}

std::string_view addon_list(const ItemTable& ast, const Item* addon) noexcept {
    if (addon == nullptr)
        return {};
    const std::string_view body = ast.text(addon->body);
    const std::size_t comma = body.find(',');
    return comma == std::string_view::npos ? std::string_view{} : body.substr(comma + 1);
}

bool addon_after(const ItemTable& ast, const Item* addon) noexcept {
    if (addon == nullptr)
        return false;
    constexpr std::size_t kAddonFlagIndex = 1;
    const std::string_view body = ast.text(addon->body);
    return body.size() > kAddonFlagIndex && body[kAddonFlagIndex] == '1';
}

}

IoIndex ConfStrEntry::default_index() const noexcept {
    return letter == SlotLetter::Load ? IoIndex{static_cast<std::uint8_t>(selentry + 1u)}
                                      : IoIndex{};
}

void ConfStr::walk_items() {
    slots_ = slots_of(ast_);
}

std::vector<ConfStrEntry> ConfStr::slots_of(const ItemTable& ast) {
    std::vector<ConfStrEntry> slots;
    std::uint32_t selentry = 0;
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

        if (it.kind == ItemKind::FileSlot || it.kind == ItemKind::MountSlot) {
            ConfStrEntry fs{};
            fs.letter = (it.kind == ItemKind::FileSlot) ? ConfStrEntry::SlotLetter::Load
                                                        : ConfStrEntry::SlotLetter::Mount;
            fs.selentry = static_cast<std::uint8_t>(selentry);
            fs.digit = it.digit;
            fs.opensave = it.opensave;
            fs.store_name = it.store_name;

            if (const std::string_view ext = ast.subfield(it, 1); !ext.empty()) {
                fs.ext.assign(ext.data(), ext.size());
            }

            fs.ioctl_index = fs.digit ? fs.digit->io_index() : fs.default_index();
            if (fs.carries_load_addr())
                fs.load_addr = parse_load_addr(ast.subfield(it, 3));
            const Item* addon = bound_addon(items, n);
            fs.addon = std::string{addon_list(ast, addon)};
            fs.addon_after = addon_after(ast, addon);
            slots.push_back(std::move(fs));
        }

        ++selentry;
    }
    return slots;
}

std::uint32_t parse_load_addr(std::string_view field) noexcept {
    constexpr unsigned kHexDigitBits = 4;
    constexpr unsigned kLetterDigitBase = 10;
    constexpr std::uint64_t kSaturated = std::numeric_limits<std::uint32_t>::max();
    std::size_t i = 0;
    while (i < field.size() && (field[i] == ' ' || (field[i] >= '\t' && field[i] <= '\r')))
        ++i;
    bool negate = false;
    if (i < field.size() && (field[i] == '+' || field[i] == '-'))
        negate = field[i++] == '-';
    constexpr std::size_t kPrefixLen = 2;
    if (i + 1 < field.size() && field[i] == '0' && (field[i + 1] == 'x' || field[i + 1] == 'X'))
        i += kPrefixLen;
    std::uint64_t v = 0;
    for (; i < field.size(); ++i) {
        const char c = field[i];
        unsigned d = 0;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a') + kLetterDigitBase;
        else if (c >= 'A' && c <= 'F')
            d = static_cast<unsigned>(c - 'A') + kLetterDigitBase;
        else
            break;
        v = (v << kHexDigitBits) | d;
        if (v > kSaturated)
            return static_cast<std::uint32_t>(kSaturated);
    }
    const auto u = static_cast<std::uint32_t>(v);
    return negate ? 0u - u : u;
}

FileSlotHit ConfStr::find_file_slot(
    ConfStrEntry::SlotLetter letter,
    FileSlotDigit digit
) const noexcept {
    return find_slot_in(slots_, letter, digit);
}

FileSlotHit ConfStr::find_slot_in(
    std::span<const ConfStrEntry> slots,
    ConfStrEntry::SlotLetter letter,
    FileSlotDigit digit
) noexcept {
    const ConfStrEntry* hit = nullptr;
    for (const ConfStrEntry& s : slots) {

        if (s.letter == letter && FileSlotDigit::or_zero(s.digit) == digit)
            hit = &s;
    }
    if (hit != nullptr)
        return FileSlotHit{infra::opt_ref_of(hit), FileSlotMatch::Exact};

    for (const ConfStrEntry& s : slots) {
        if (s.selentry == 0 && s.letter == letter)
            return FileSlotHit{infra::OptRef<const ConfStrEntry>{s}, FileSlotMatch::RowZero};
    }
    return FileSlotHit{};
}

FileSlotHit ConfStr::find_load_slot(FileSlotDigit digit) const noexcept {
    return find_slot_in(slots_, ConfStrEntry::SlotLetter::Load, digit);
}

std::optional<ConfStrFileRow> ConfStr::menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept {
    const auto items = ast_.items();
    const std::size_t row = ItemTable::index_of(item);
    if (row >= items.size())
        return std::nullopt;
    const Item& it = items[row];
    if (it.kind != ItemKind::FileSlot)
        return std::nullopt;

    const Item* addon = bound_addon(items, row);
    return ConfStrFileRow{
        ast_.subfield(it, 1),
        drawn,
        parse_load_addr(ast_.subfield(it, 3)),
        it.opensave,
        addon_list(ast_, addon),
        addon_after(ast_, addon)
    };
}

std::vector<ConfStrFileRow> ConfStr::remembered_rows() const {
    std::vector<ConfStrFileRow> rows;
    for (const Item& it : ast_.items()) {
        if (it.kind != ItemKind::FileSlot || !it.store_name || !it.digit)
            continue;
        rows.push_back(
            ConfStrFileRow{
                ast_.subfield(it, 1),
                it.digit->io_index(),
                parse_load_addr(ast_.subfield(it, 3)),
                it.opensave
            }
        );
    }
    return rows;
}

std::vector<IoIndex> ConfStr::remembered_mounts() const {
    std::vector<IoIndex> slots;
    for (const Item& it : ast_.items()) {
        if (it.kind != ItemKind::MountSlot || !it.store_name || !it.digit)
            continue;
        slots.push_back(it.digit->io_index());
    }
    return slots;
}

BlockSlotMask ConfStr::mount_slot_mask() const noexcept {
    BlockSlotMask mask{};
    for (const Item& it : ast_.items()) {
        if (it.kind != ItemKind::MountSlot)
            continue;

        mask = mask.with_or_all(FileSlotDigit::or_zero(it.digit).slot());
    }
    return mask;
}

std::uint8_t ConfStr::ext_subindex(std::string_view filename, std::string_view ext_list) noexcept {
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
