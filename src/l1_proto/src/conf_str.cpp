// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/conf_str.h"
#include "conf_str_detail.h"

#include <optional>

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

void ConfStr::walk_items() {
    slots_ = FileSlots::of(ast_);
}

std::optional<ConfStrFileRow> ConfStr::menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept {
    const auto items = ast_.items();
    const std::size_t row = ItemTable::index_of(item);
    if (row >= items.size())
        return std::nullopt;
    const Item& it = items[row];
    if (it.kind != ItemKind::FileSlot)
        return std::nullopt;

    const Item* addon = detail::bound_addon(items, &it);
    return ConfStrFileRow{
        ast_.subfield(it, 1),
        drawn,
        parse_load_addr(ast_.subfield(it, 3)),
        it.opensave,
        detail::addon_list(ast_, addon),
        detail::addon_after(ast_, addon)
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

}
