// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "reactor/notifier.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "os/types.h"
#include "proto/conf_str_caps.h"
#include "proto/irq_binding.h"
#include "proto/item_table.h"
#include "proto/save_state_decl.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

struct ConfStrEntry {
    TASTY_SEAT_EXEMPT(const_shared);

    enum class SlotLetter : std::uint8_t { Load, Mount };
    SlotLetter letter = SlotLetter::Load;
    std::uint8_t selentry = 0;

    std::optional<FileSlotDigit> digit{};
    bool opensave = false;
    bool store_name = false;
    std::string ext;

    IoIndex ioctl_index{};

    std::uint32_t load_addr = 0;
    std::string addon;
    bool addon_after = false;

    [[nodiscard]] IoIndex default_index() const noexcept;

    [[nodiscard]] constexpr bool carries_load_addr() const noexcept {
        return letter == SlotLetter::Load;
    }
};

struct ConfStrFileRow {
    std::string_view ext;
    IoIndex ioctl_index{};
    std::uint32_t load_addr = 0;
    bool opensave = false;
    std::string_view addon{};
    bool addon_after = false;
};

enum class FileSlotMatch : std::uint8_t { Exact, RowZero };

struct FileSlotHit {
    infra::OptRef<const ConfStrEntry> entry{};
    FileSlotMatch how = FileSlotMatch::Exact;
};

[[nodiscard]] std::uint32_t parse_load_addr(std::string_view field) noexcept;

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

    const std::vector<ConfStrEntry>& file_slots() const noexcept { return slots_; }

    [[nodiscard]] FileSlotHit find_file_slot(
        ConfStrEntry::SlotLetter letter,
        FileSlotDigit digit
    ) const noexcept;

    [[nodiscard]] FileSlotHit find_load_slot(FileSlotDigit digit) const noexcept;

    [[nodiscard]] static std::vector<ConfStrEntry> slots_of(const ItemTable& ast);
    [[nodiscard]] static FileSlotHit find_slot_in(
        std::span<const ConfStrEntry> slots,
        ConfStrEntry::SlotLetter letter,
        FileSlotDigit digit
    ) noexcept;

    std::optional<ConfStrFileRow> menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept;

    [[nodiscard]] std::vector<ConfStrFileRow> remembered_rows() const;

    [[nodiscard]] std::vector<IoIndex> remembered_mounts() const;

    [[nodiscard]] BlockSlotMask mount_slot_mask() const noexcept;

    static std::uint8_t ext_subindex(std::string_view filename, std::string_view ext_list) noexcept;

    [[nodiscard]] static WideIoIndex wire_index(
        ConfStrFileRow row,
        std::string_view filename
    ) noexcept {
        return WideIoIndex::of(row.ioctl_index, ext_subindex(filename, row.ext));
    }

    [[nodiscard]] static WideIoIndex wire_index(
        const ConfStrEntry& slot,
        std::string_view filename
    ) noexcept {
        return wire_index(row_of(slot), filename);
    }

    static ConfStrFileRow row_of(const ConfStrEntry& slot) noexcept {
        return ConfStrFileRow{
            slot.ext,
            slot.ioctl_index,
            slot.load_addr,
            slot.opensave,
            slot.addon,
            slot.addon_after
        };
    }

private:
    ConfStr() = default;

    void walk_items();
    std::string name_;
    ConfStrCaps caps_;
    std::vector<std::string> items_;
    ItemTable ast_;
    std::vector<ConfStrEntry> slots_;
};

}
