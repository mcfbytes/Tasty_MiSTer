// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "reactor/notifier.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "os/types.h"
#include "proto/conf_str_caps.h"
#include "proto/file_slots.h"
#include "proto/irq_binding.h"
#include "proto/item_table.h"
#include "proto/save_state_decl.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

class ConfStr {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] static Ex<ConfStr> parse(std::string_view raw, hal::PhysRegion aperture);

    const std::string& name() const noexcept { return name_; }

    const std::vector<Capability>& capabilities() const noexcept { return caps_.capabilities(); }

    const std::vector<IrqBinding>& irq_bindings() const noexcept { return caps_.irq_bindings(); }

    const std::vector<AxiLwDecl>& lw_windows() const noexcept { return caps_.lw_decls(); }

    const std::vector<AxiDdrDecl>& ddr_windows() const noexcept { return caps_.ddr_decls(); }

    const std::vector<AxiUnkindedDecl>& unkinded_axi_windows() const noexcept {
        return caps_.unkinded_decls();
    }

    std::uint32_t axi_unknown_kind_count() const noexcept { return caps_.axi_unknown_kind_count(); }

    const std::optional<SaveStateDecl>& savestate() const noexcept { return caps_.savestate(); }

    std::optional<os::UioLine> irq_line_for(reactor::Cause klass) const {
        return caps_.irq_line_for(klass);
    }

    const std::vector<std::string>& raw_items() const noexcept { return items_; }

    ItemTable item_table() const { return ast_; }

    [[nodiscard]] bool declares_cheats() const noexcept {
        for (const Item& it : ast_.items()) {
            if (it.kind == ItemKind::Cheats)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool declares_info() const noexcept { return ast_.info_row().has_value(); }

    [[nodiscard]] bool declares_turbo() const noexcept {
        for (const Item& it : ast_.items()) {
            if (it.kind == ItemKind::Turbo)
                return true;
        }
        return false;
    }

    [[nodiscard]] std::string_view default_manifest() const noexcept {
        std::string_view out{};
        for (const Item& it : ast_.items()) {
            if (it.kind != ItemKind::DefMra)
                continue;
            const std::string_view f = ast_.text(it.body);
            constexpr std::string_view kDefMraToken = "DEFMRA,";
            out = f.size() > kDefMraToken.size() ? f.substr(kDefMraToken.size())
                                                 : std::string_view{};
        }
        return out;
    }

    std::string_view button_list(int type) const noexcept;

    const FileSlots& file_slots() const noexcept { return slots_; }

    std::optional<ConfStrFileRow> menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept;

    [[nodiscard]] std::vector<ConfStrFileRow> remembered_rows() const;

    [[nodiscard]] std::vector<IoIndex> remembered_mounts() const;

    [[nodiscard]] BlockSlotMask mount_slot_mask() const noexcept;

private:
    ConfStr() = default;

    void walk_items();
    std::string name_;
    ConfStrCaps caps_;
    std::vector<std::string> items_;
    ItemTable ast_;
    FileSlots slots_;
};

}
