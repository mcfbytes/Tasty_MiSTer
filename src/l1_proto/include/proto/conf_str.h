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
#include "reactor/notifier.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "os/types.h"
#include "proto/irq_binding.h"
#include "proto/item_table.h"
#include "proto/save_state_decl.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

enum class AxiKind : std::uint8_t { Lw, Ddr, Unknown };

struct AxiLwDecl {
    hal::LwOffset offset;
    std::uint32_t size;
};

struct AxiDdrDecl {
    hal::FabricAddr base;
    std::uint32_t size;
};

struct AxiRawBase {
    std::uint32_t v = 0;
    friend constexpr bool operator==(AxiRawBase, AxiRawBase) = default;
};

struct AxiUnkindedDecl {
    AxiRawBase value;
    std::uint32_t size;
};

struct Capability {
    enum class Kind : std::uint8_t { SaveState, Uart, Midi, Irq, Axi, Unknown };
    Kind kind;
    std::string raw;
};

struct ConfStrEntry {
    char letter = 0;
    std::uint8_t selentry = 0;
    std::uint8_t digit = 0;
    bool has_digit = false;
    bool opensave = false;
    bool store_name = false;
    std::string ext;

    std::uint8_t ioctl_index = 0;

    std::uint32_t load_addr = 0;
};

struct ConfStrFileRow {
    std::string_view ext;
    std::uint8_t ioctl_index = 0;
    std::uint32_t load_addr = 0;
    bool opensave = false;
};

enum class FileSlotMatch : std::uint8_t { Exact, RowZero };

struct FileSlotHit {
    const ConfStrEntry* entry = nullptr;
    FileSlotMatch how = FileSlotMatch::Exact;
};

[[nodiscard]] std::uint32_t parse_load_addr(std::string_view field) noexcept;

[[nodiscard]] std::uint8_t decode_hd_selector(std::string_view after_letter) noexcept;

enum class Selectable : std::uint8_t { Never, Always, Conditional };

enum class MatchPhase : std::uint8_t { BeforePrefixes, AfterPrefixes };

enum class BootEffect : std::uint8_t { None, Fires };

enum class HeadMatch : std::uint8_t { Letter, Exact, Prefix, PrefixIcase };

struct ItemRule {
    std::string_view head;
    char letter;
    HeadMatch match;
    ItemKind kind;
    ExShift ex;
    Selectable sel;
    MatchPhase phase;
    BootEffect boot;
    std::string_view evidence;
};

inline constexpr std::size_t kItemRuleCount = 22;

inline constexpr std::array<ItemRule, kItemRuleCount> kItemRules{{
    {"DEFMRA,", 0, HeadMatch::Prefix, ItemKind::DefMra, ExShift::None, Selectable::Never,
     MatchPhase::BeforePrefixes, BootEffect::Fires,
     "menu.cpp:1941 renders nothing; user_io.cpp:829 stores the default MRA path"},
    {"DIP", 0, HeadMatch::Exact, ItemKind::Dip, ExShift::None, Selectable::Conditional,
     MatchPhase::BeforePrefixes, BootEffect::None,
     "menu.cpp:1944-1954 — one row, but only when `!page && arcade_sw()->dip_num`"},
    {"TURBO", 0, HeadMatch::PrefixIcase, ItemKind::Turbo, ExShift::None, Selectable::Always,
     MatchPhase::BeforePrefixes, BootEffect::None,
     "support/n64/n64.cpp:610 scans the raw field; menu.cpp:2127's T arm draws "
     "it as a selectable row whose bit ref (\"URBO\") never decodes"},
    {"", 'P', HeadMatch::Letter, ItemKind::PageDecl, ExShift::None, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::None,
     "menu.cpp:1993-2021 — `P<n>,` is the submenu link row; `P<n>` with no "
     "comma is membership and is stripped by the prefix pass instead"},
    {"", 'F', HeadMatch::Letter, ItemKind::FileSlot, ExShift::None, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2023-2091; user_io.cpp:931 autoloads an `FC` slot at boot"},
    {"", 'S', HeadMatch::Letter, ItemKind::MountSlot, ExShift::None, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2023-2091; user_io.cpp:975 automounts an `SC` slot at boot"},
    {"", 'C', HeadMatch::Letter, ItemKind::Cheats, ExShift::None, Selectable::Conditional,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2094-2124 — ONE row, or TWO when game_docs_manual_available(); "
     "user_io.cpp:926 sets use_cheats"},
    {"", 'T', HeadMatch::Letter, ItemKind::Toggle, ExShift::None, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2127-2138; user_io.cpp:844-854 registers the bit, width must be 1"},
    {"", 't', HeadMatch::Letter, ItemKind::Toggle, ExShift::Plus32, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2127; user_io.cpp:846 passes `ex` for the lowercase spelling"},
    {"", 'R', HeadMatch::Letter, ItemKind::ToggleClose, ExShift::None, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2127 renders it exactly as T; the OSD close is MAIN2's arm"},
    {"", 'r', HeadMatch::Letter, ItemKind::ToggleClose, ExShift::Plus32, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2127; user_io.cpp:846 passes `ex` for the lowercase spelling"},
    {"", 'O', HeadMatch::Letter, ItemKind::Option, ExShift::None, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2140-2188; user_io.cpp:855-875 registers the whole span"},
    {"", 'o', HeadMatch::Letter, ItemKind::Option, ExShift::Plus32, Selectable::Always,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "menu.cpp:2140; user_io.cpp:860 passes `ex` for the lowercase spelling"},
    {"", '-', HeadMatch::Letter, ItemKind::Separator, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::None,
     "menu.cpp:2190-2199 — drawn, never landed on; it also clears `last_space`"},
    {"", 'f', HeadMatch::Letter, ItemKind::Addon, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::None,
     "menu.cpp:2385-2389 — latched by MAIN2, bound to the NEXT F/S row; "
     "`p[0] < 'A'` at :2382 is why it takes no ordinal"},
    {"jn", 0, HeadMatch::Prefix, ItemKind::JoyNames, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::None,
     "input.cpp:6484 get_btn(1); no menu arm draws it"},
    {"jp", 0, HeadMatch::Prefix, ItemKind::JoyNames, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::None,
     "input.cpp:6484 get_btn(2); no menu arm draws it"},
    {"", 'J', HeadMatch::Letter, ItemKind::JoyNames, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "input.cpp:6484 get_btn(0); user_io.cpp:877-889 sets joy_transl/joy_force"},
    {"", 'I', HeadMatch::Letter, ItemKind::Info, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::None,
     "user_io.cpp:2648 show_core_info(); no menu arm draws it"},
    {"", 'V', HeadMatch::Letter, ItemKind::Version, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "user_io.cpp:907-915 appends the version to the OSD core name"},
    {"", 'v', HeadMatch::Letter, ItemKind::CfgVer, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::Fires,
     "user_io.cpp:917-925 sets config_ver, which renames <core>_v%d.CFG"},
    {"", 'X', HeadMatch::Letter, ItemKind::NoOsd, ExShift::None, Selectable::Never,
     MatchPhase::AfterPrefixes, BootEffect::Fires, "user_io.cpp:902-905 sets disable_osd"},
}};

consteval bool every_kind_has_a_rule() {
    for (unsigned k = 0; k < static_cast<unsigned>(ItemKind::kCount); ++k) {
        if (k == static_cast<unsigned>(ItemKind::Unknown)) continue;
        bool seen = false;
        for (const ItemRule& r : kItemRules) {
            if (static_cast<unsigned>(r.kind) == k) seen = true;
        }
        if (!seen) return false;
    }
    return true;
}
static_assert(every_kind_has_a_rule(),
              "a new ItemKind with no rule is a token the parser reports as "
              "Unknown at runtime instead of failing here, at the desk");

consteval bool every_rule_has_exactly_one_head() {
    for (const ItemRule& r : kItemRules) {
        if (r.head.empty() == (r.letter == 0)) return false;
        if (!r.head.empty() && r.match == HeadMatch::Letter) return false;
        if (r.letter != 0 && r.match != HeadMatch::Letter) return false;
    }
    return true;
}
static_assert(every_rule_has_exactly_one_head());

[[nodiscard]] constexpr Selectable selectable_of(ItemKind k) noexcept {
    for (const ItemRule& r : kItemRules) {
        if (r.kind == k) return r.sel;
    }
    return Selectable::Never;
}

consteval bool every_kind_agrees_with_itself() {
    for (const ItemRule& a : kItemRules) {
        for (const ItemRule& b : kItemRules) {
            if (a.kind == b.kind && a.sel != b.sel) return false;
        }
    }
    return true;
}
static_assert(every_kind_agrees_with_itself(),
              "selectable_of() answers for a KIND, so two rows of one kind "
              "disagreeing about `sel` would make the answer depend on order");

struct CondRule {
    char letter;
    CondEffect effect;
    bool acts_when_set;
};

inline constexpr std::array<CondRule, 4> kCondRules{{
    {'H', CondEffect::Hide, true},
    {'h', CondEffect::Hide, false},
    {'D', CondEffect::Disable, true},
    {'d', CondEffect::Disable, false},
}};

class ConfStr {
    TASTY_SEAT_EXEMPT(main);

public:
    static Ex<ConfStr> parse(std::string_view raw, hal::PhysRegion aperture);

    const std::string& name() const noexcept { return name_; }

    const std::vector<Capability>& capabilities() const noexcept { return caps_; }
    const std::vector<IrqBinding>& irq_bindings() const noexcept { return irq_; }

    const std::vector<AxiLwDecl>& lw_windows() const noexcept { return axi_lw_; }
    const std::vector<AxiDdrDecl>& ddr_windows() const noexcept { return axi_ddr_; }

    const std::vector<AxiUnkindedDecl>& unkinded_axi_windows() const noexcept {
        return axi_unkinded_;
    }

    std::uint32_t axi_unknown_kind_count() const noexcept { return axi_unknown_kinds_; }
    const std::optional<SaveStateDecl>& savestate() const noexcept { return ss_; }

    std::optional<os::UioLine> irq_line_for(reactor::Cause klass) const;

    const std::vector<std::string>& raw_items() const noexcept { return items_; }

    ItemTable item_table() const { return ast_; }

    [[nodiscard]] bool declares_cheats() const noexcept {
        for (const Item& it : ast_.items()) {
            if (it.kind == ItemKind::Cheats) return true;
        }
        return false;
    }

    [[nodiscard]] bool declares_turbo() const noexcept {
        for (const Item& it : ast_.items()) {
            if (it.kind == ItemKind::Turbo) return true;
        }
        return false;
    }

    [[nodiscard]] std::string_view default_manifest() const noexcept {
        std::string_view out{};
        for (const Item& it : ast_.items()) {
            if (it.kind != ItemKind::DefMra) continue;
            const std::string_view f = ast_.text(it.body);
            out = f.size() > 7 ? f.substr(7) : std::string_view{};
        }
        return out;
    }

    std::string_view button_list(int type) const noexcept;

    const std::vector<ConfStrEntry>& file_slots() const noexcept { return slots_; }

    [[nodiscard]] FileSlotHit find_file_slot(char type, std::uint8_t index) const noexcept;

    [[nodiscard]] FileSlotHit find_load_slot(FileSlotDigit digit) const noexcept;

    [[nodiscard]] static std::vector<ConfStrEntry> slots_of(const ItemTable& ast);
    [[nodiscard]] static FileSlotHit find_slot_in(std::span<const ConfStrEntry> slots, char type,
                                                  std::uint8_t index) noexcept;

    std::optional<ConfStrFileRow> menu_pick(ItemOrdinal item, IoIndex drawn) const noexcept;

    static std::uint8_t ext_subindex(std::string_view filename, std::string_view ext_list) noexcept;

    static std::uint16_t wire_index(ConfStrFileRow row, std::string_view filename) noexcept {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(ext_subindex(filename, row.ext)) << 6) | row.ioctl_index);
    }
    static std::uint16_t wire_index(const ConfStrEntry& slot, std::string_view filename) noexcept {
        return wire_index(row_of(slot), filename);
    }
    static ConfStrFileRow row_of(const ConfStrEntry& slot) noexcept {
        return ConfStrFileRow{slot.ext, slot.ioctl_index, slot.load_addr, slot.opensave};
    }

private:
    ConfStr() = default;

    void walk_items();
    std::string name_;
    std::vector<Capability> caps_;
    std::vector<IrqBinding> irq_;
    std::vector<AxiLwDecl> axi_lw_;
    std::vector<AxiDdrDecl> axi_ddr_;
    std::vector<AxiUnkindedDecl> axi_unkinded_;
    std::uint32_t axi_unknown_kinds_ = 0;
    std::optional<SaveStateDecl> ss_;
    std::vector<std::string> items_;
    ItemTable ast_;
    std::vector<ConfStrEntry> slots_;
};

}  // namespace mister::proto
