// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/page_description.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "proto/types.h"
#include "ui/build_env.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::ui {

class ISlotLabels;
class IOptionHost;

using proto::BitField;

class OptionItem {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class Kind : std::uint8_t {
        Page,
        File,
        Image,
        Cheats,
        Manual,
        Dip,
        Toggle,
        ToggleClose,
        Option,
        Separator,
        Addon,
        DefMra,
        JoyNames,
        Info,
        Version,
        CfgVer,
        NoOsd,
        Unknown,
        Exit,
        kCount,
    };

    struct Carry {
        std::string_view addon;
        bool addon_after = false;
        bool last_space = false;
    };

    struct Source {
        const proto::ItemTable* table = nullptr;
        proto::ItemOrdinal item{};
        RowCount taken{};
        Carry carry{};
    };

    struct Look {
        bool flat = false;
        std::uint32_t value = 0;
    };

    struct Press {
        bool select = false;
        bool minus = false;
        bool at_root = false;
        std::uint32_t value = 0;
    };

    virtual ~OptionItem() = default;
    OptionItem(const OptionItem&) = delete;
    OptionItem& operator=(const OptionItem&) = delete;
    OptionItem(OptionItem&&) = delete;
    OptionItem& operator=(OptionItem&&) = delete;

    [[nodiscard]] static std::unique_ptr<OptionItem> make(Kind k);

    [[nodiscard]] Carry build(const Source& in, const BuildEnv& env);
    [[nodiscard]] std::string compose(const Look& look) const;

    [[nodiscard]] app::PageDescription::Row describe(const Look& look) const;
    [[nodiscard]] Transition activate(const Press& press, IOptionHost& host) const;

    [[nodiscard]] virtual bool hidden_in_flat_root() const noexcept { return false; }

    [[nodiscard]] virtual bool leads_with_manual_row() const noexcept { return false; }

    [[nodiscard]] virtual bool rings_at_root() const noexcept { return false; }

    [[nodiscard]] virtual bool opens_browser() const noexcept { return false; }

    [[nodiscard]] virtual bool mounts_image() const noexcept { return false; }

    const Kind kind;
    bool disabled = false;
    bool store_name = false;
    bool ext_word = false;
    std::uint8_t page = 0;
    std::uint8_t target_page = 0;

    std::uint8_t ioctl_index = 0;

    proto::ItemOrdinal item{};
    BitField bits{};
    std::string label;
    std::string ext_list;
    std::string addon_ext;
    bool addon_after = false;
    std::vector<std::string> values;

    std::optional<RowIndex> selectable_index;

    std::optional<proto::OsdRow> screen_row;

protected:
    explicit OptionItem(Kind k) noexcept : kind(k) {}

private:
    virtual Carry on_build(const Source& in, const BuildEnv& env) = 0;
    virtual std::string on_compose(const Look& look) const = 0;
    virtual app::PageDescription::Row on_describe(const Look& look) const = 0;
    virtual Transition on_activate(const Press& press, IOptionHost& host) const = 0;
};

struct KindRow {
    OptionItem::Kind kind;
    bool draws;
    bool selects;
    std::string_view evidence;
};

inline constexpr std::size_t kKindRowCount = static_cast<std::size_t>(OptionItem::Kind::kCount);

inline constexpr std::array<KindRow, kKindRowCount> kKindRows{{
    {OptionItem::Kind::Page, true, true, "menu.cpp:1993-2021"},
    {OptionItem::Kind::File, true, true, "menu.cpp:2023-2091"},
    {OptionItem::Kind::Image, true, true, "menu.cpp:2023-2091"},
    {OptionItem::Kind::Cheats,
     true,
     true,
     "menu.cpp:2106-2123 — drawn always, stippled when !cheats_available()"},
    {OptionItem::Kind::Manual,
     true,
     true,
     "menu.cpp:2096-2104 — emitted BEFORE Cheats, own ordinal (req)"},
    {OptionItem::Kind::Dip, true, true, "menu.cpp:1944-1954 — emitted ONLY when it is selectable"},
    {OptionItem::Kind::Toggle, true, true, "menu.cpp:2127-2138"},
    {OptionItem::Kind::ToggleClose, true, true, "menu.cpp:2127-2138; the close is MAIN2's arm"},
    {OptionItem::Kind::Option, true, true, "menu.cpp:2140-2188"},
    {OptionItem::Kind::Separator, true, false, "menu.cpp:2190-2199 — drawn, never landed on"},
    {OptionItem::Kind::Addon,
     false,
     false,
     "menu.cpp:2385-2389 — latched; the `continue` at :2388 is before the :2398 counter"},
    {OptionItem::Kind::DefMra,
     false,
     false,
     "menu.cpp:1941 — an erratum's headline: drawn by neither pass"},
    {OptionItem::Kind::JoyNames, false, false, "input.cpp:6484 — no menu arm draws it"},
    {OptionItem::Kind::Info, false, false, "user_io.cpp:2648 — no menu arm draws it"},
    {OptionItem::Kind::Version, false, false, "user_io.cpp:907-915 — boot effect only"},
    {OptionItem::Kind::CfgVer, false, false, "user_io.cpp:917-925 — boot effect only"},
    {OptionItem::Kind::NoOsd, false, false, "user_io.cpp:902-905 — boot effect only"},
    {OptionItem::Kind::Unknown, false, false, "no rule matched; stock draws nothing"},
    {OptionItem::Kind::Exit, true, true, "menu.cpp:2208-2218 — synthesised, STD_EXIT / STD_BACK"},
}};

consteval bool kind_rows_are_indexed_by_kind() {
    for (std::size_t i = 0; i < kKindRowCount; ++i) {
        if (static_cast<std::size_t>(kKindRows[i].kind) != i)
            return false;
    }
    return true;
}

static_assert(kind_rows_are_indexed_by_kind());

consteval bool selects_iff_draws_and_not_a_separator() {
    std::size_t drawn_only = 0;
    for (const KindRow& r : kKindRows) {
        if (r.selects && !r.draws)
            return false;
        if (r.draws && !r.selects)
            ++drawn_only;
    }
    return drawn_only == 1
        && !kKindRows[static_cast<std::size_t>(OptionItem::Kind::Separator)].selects;
}

static_assert(
    selects_iff_draws_and_not_a_separator(),
    "a kind that draws but takes no ordinal (or the reverse) is "
    "exactly the shape of an erratum"
);

[[nodiscard]] constexpr bool kind_draws(OptionItem::Kind k) noexcept {
    return kKindRows[static_cast<std::size_t>(k)].draws;
}

[[nodiscard]] constexpr bool kind_selects(OptionItem::Kind k) noexcept {
    return kKindRows[static_cast<std::size_t>(k)].selects;
}

struct KindMapRow {
    proto::ItemKind from;
    OptionItem::Kind to;
};

inline constexpr std::array<KindMapRow, static_cast<std::size_t>(proto::ItemKind::kCount)> kKindMap{
    {
        {proto::ItemKind::Option, OptionItem::Kind::Option},
        {proto::ItemKind::Toggle, OptionItem::Kind::Toggle},
        {proto::ItemKind::ToggleClose, OptionItem::Kind::ToggleClose},
        {proto::ItemKind::FileSlot, OptionItem::Kind::File},
        {proto::ItemKind::MountSlot, OptionItem::Kind::Image},
        {proto::ItemKind::Addon, OptionItem::Kind::Addon},
        {proto::ItemKind::JoyNames, OptionItem::Kind::JoyNames},
        {proto::ItemKind::Cheats, OptionItem::Kind::Cheats},
        {proto::ItemKind::Info, OptionItem::Kind::Info},
        {proto::ItemKind::Version, OptionItem::Kind::Version},
        {proto::ItemKind::CfgVer, OptionItem::Kind::CfgVer},
        {proto::ItemKind::NoOsd, OptionItem::Kind::NoOsd},
        {proto::ItemKind::PageDecl, OptionItem::Kind::Page},
        {proto::ItemKind::Separator, OptionItem::Kind::Separator},
        {proto::ItemKind::Dip, OptionItem::Kind::Dip},
        {proto::ItemKind::DefMra, OptionItem::Kind::DefMra},
        {proto::ItemKind::Turbo, OptionItem::Kind::Toggle},
        {proto::ItemKind::Unknown, OptionItem::Kind::Unknown},
    }
};

consteval bool every_item_kind_maps_exactly_once() {
    for (unsigned k = 0; k < static_cast<unsigned>(proto::ItemKind::kCount); ++k) {
        unsigned n = 0;
        for (const KindMapRow& r : kKindMap) {
            if (static_cast<unsigned>(r.from) == k)
                ++n;
        }
        if (n != 1)
            return false;
    }
    return true;
}

static_assert(every_item_kind_maps_exactly_once());

[[nodiscard]] constexpr OptionItem::Kind kind_of(proto::ItemKind k) noexcept {
    for (const KindMapRow& r : kKindMap) {
        if (r.from == k)
            return r.to;
    }
    return OptionItem::Kind::Unknown;
}

std::uint32_t cycle_value(BitField bits, std::uint32_t current, int dir, const ISlotLabels* labels);

}
