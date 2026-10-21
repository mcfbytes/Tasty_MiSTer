// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/conf_str.h"
#include "conf_str_detail.h"

#include <cctype>
#include <optional>

namespace mister::proto {

Ex<ConfStr> ConfStr::parse(std::string_view raw, hal::PhysRegion aperture) {
    ConfStr c;
    const auto fields = split(raw, ';');
    if (fields.empty()) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 0});
    }
    c.name_ = std::string(fields[0]);

    if (fields.size() >= 2) {
        const std::string_view f1 = fields[1];
        std::size_t pos = 0;
        while (pos < f1.size()) {
            const std::size_t seg_start = pos;

            pos = c.caps_.try_ss(f1, pos, aperture);
            pos = c.caps_.try_uart(f1, pos);
            pos = c.caps_.try_midi(f1, pos);
            const auto irq = c.caps_.try_irq(f1, pos);
            if (!irq)
                return std::unexpected(irq.error());
            pos = *irq;
            const auto axi = c.caps_.try_axi(f1, pos);
            if (!axi)
                return std::unexpected(axi.error());
            pos = *axi;

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

    for (std::size_t i = 2; i < fields.size(); ++i) {
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
    for (const std::string& raw : items_) {
        const std::string_view p{raw};

        if (p.empty())
            break;

        bool hit = false;
        if (type == 0) {
            hit = (p[0] == 'J');
        } else if (p.size() >= 2 && p[0] == 'j') {
            hit = (type == 1 && p[1] == 'n') || (type == 2 && p[1] == 'p');
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

std::uint8_t view_page(const Item& it) noexcept {
    return it.declares_page ? 0 : it.page;
}

const Item* bound_addon(std::span<const Item> items, std::size_t row) noexcept {
    const std::uint8_t page = view_page(items[row]);
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
    const std::string_view body = ast.text(addon->body);
    return body.size() >= 2 && body[1] == '1';
}

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

        if (!it.declares_page && it.page != 0)
            continue;

        const Selectable sel = selectable_of(it.kind);
        if (sel == Selectable::Never)
            continue;

        if (it.kind == ItemKind::FileSlot || it.kind == ItemKind::MountSlot) {
            ConfStrEntry fs{};
            fs.letter = (it.kind == ItemKind::FileSlot) ? 'F' : 'S';
            fs.selentry = static_cast<std::uint8_t>(selentry);
            fs.digit = it.digit;
            fs.has_digit = it.has_digit;
            fs.opensave = it.opensave;
            fs.store_name = it.store_name;

            if (const std::string_view ext = ast.subfield(it, 1); !ext.empty()) {
                fs.ext.assign(ext.data(), ext.size());
            }

            fs.ioctl_index = fs.has_digit ? fs.digit
                : (fs.letter == 'F')      ? static_cast<std::uint8_t>(selentry + 1u)
                                          : static_cast<std::uint8_t>(0u);
            if (fs.letter == 'F')
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
    std::size_t i = 0;
    while (i < field.size() && (field[i] == ' ' || (field[i] >= '\t' && field[i] <= '\r')))
        ++i;
    bool negate = false;
    if (i < field.size() && (field[i] == '+' || field[i] == '-'))
        negate = field[i++] == '-';
    if (i + 1 < field.size() && field[i] == '0' && (field[i + 1] == 'x' || field[i + 1] == 'X'))
        i += 2;
    std::uint64_t v = 0;
    for (; i < field.size(); ++i) {
        const char c = field[i];
        unsigned d = 0;
        if (c >= '0' && c <= '9')
            d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = static_cast<unsigned>(c - 'A' + 10);
        else
            break;
        v = (v << 4) | d;
        if (v > 0xFFFF'FFFFu)
            return 0xFFFF'FFFFu;
    }
    const auto u = static_cast<std::uint32_t>(v);
    return negate ? static_cast<std::uint32_t>(0u - u) : u;
}

FileSlotHit ConfStr::find_file_slot(char type, std::uint8_t index) const noexcept {
    return find_slot_in(slots_, type, index);
}

FileSlotHit ConfStr::find_slot_in(
    std::span<const ConfStrEntry> slots,
    char type,
    std::uint8_t index
) noexcept {
    const char want = (type >= 'a' && type <= 'z') ? static_cast<char>(type - 0x20) : type;
    const ConfStrEntry* hit = nullptr;
    for (const ConfStrEntry& s : slots) {

        const std::uint8_t num = s.has_digit ? s.digit : 0u;
        if (s.letter == want && num == index)
            hit = &s;
    }
    if (hit != nullptr)
        return FileSlotHit{infra::opt_ref_of(hit), FileSlotMatch::Exact};

    for (const ConfStrEntry& s : slots) {
        if (s.selentry == 0 && s.letter == want)
            return FileSlotHit{infra::OptRef<const ConfStrEntry>{s}, FileSlotMatch::RowZero};
    }
    return FileSlotHit{};
}

FileSlotHit ConfStr::find_load_slot(FileSlotDigit digit) const noexcept {
    return find_file_slot('F', digit.v);
}

std::optional<ConfStrFileRow> ConfStr::menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept {
    const auto items = ast_.items();
    if (item.v >= items.size())
        return std::nullopt;
    const Item& it = items[item.v];
    if (it.kind != ItemKind::FileSlot)
        return std::nullopt;

    const Item* addon = bound_addon(items, item.v);
    return ConfStrFileRow{
        ast_.subfield(it, 1),
        drawn.v,
        parse_load_addr(ast_.subfield(it, 3)),
        it.opensave,
        addon_list(ast_, addon),
        addon_after(ast_, addon)
    };
}

std::vector<ConfStrFileRow> ConfStr::remembered_rows() const {
    std::vector<ConfStrFileRow> rows;
    for (const Item& it : ast_.items()) {
        if (it.kind != ItemKind::FileSlot || !it.store_name || !it.has_digit)
            continue;
        rows.push_back(
            ConfStrFileRow{
                ast_.subfield(it, 1),
                it.digit,
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
        if (it.kind != ItemKind::MountSlot || !it.store_name || !it.has_digit)
            continue;
        slots.push_back(IoIndex{it.digit});
    }
    return slots;
}

std::uint8_t ConfStr::ext_subindex(std::string_view filename, std::string_view ext_list) noexcept {
    const std::size_t dot = filename.rfind('.');
    if (dot == std::string_view::npos)
        return 0;

    char e[3] = {' ', ' ', ' '};
    for (std::size_t i = 0; i < 3 && dot + 1 + i < filename.size(); ++i) {
        e[i] = filename[dot + 1 + i];
    }
    std::uint8_t idx = 0;
    std::string_view rest = ext_list;
    while (!rest.empty()) {
        bool found = true;
        for (std::size_t i = 0; i < 3; ++i) {
            const char c = i < rest.size() ? rest[i] : ' ';
            if (c == '*')
                break;
            if (c == '?')
                continue;
            const auto a = static_cast<unsigned char>(c);
            const auto b = static_cast<unsigned char>(e[i]);
            if (std::toupper(a) != std::toupper(b)) {
                found = false;
                break;
            }
        }
        if (found)
            return idx;
        if (rest.size() <= 3)
            break;
        ++idx;
        rest.remove_prefix(3);
    }
    return 0;
}

}
