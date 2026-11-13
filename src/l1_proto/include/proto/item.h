// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "proto/bit_field.h"
#include "proto/status_bit.h"
#include "proto/text_span.h"
#include "proto/types.h"

namespace mister::proto {

enum class ItemKind : std::uint8_t {
    Option,
    Toggle,
    ToggleClose,
    FileSlot,
    MountSlot,
    Addon,
    JoyNames,
    Cheats,
    Info,
    Version,
    CfgVer,
    NoOsd,
    PageDecl,
    Separator,
    Dip,
    DefMra,
    Turbo,
    Unknown,
    kCount,
};

[[nodiscard]] constexpr std::optional<FileRowKind> file_row_kind_of(ItemKind k) noexcept {
    if (k == ItemKind::FileSlot)
        return FileRowKind::File;
    if (k == ItemKind::MountSlot)
        return FileRowKind::Mount;
    return std::nullopt;
}

enum class CondEffect : std::uint8_t { Hide, Disable };

struct Cond {
    CondEffect effect = CondEffect::Hide;
    bool acts_when_set = false;
    StatusBit bit{};
    friend constexpr bool operator==(Cond, Cond) = default;
};

inline constexpr std::size_t kMaxConds = 8;

struct Visibility {
    bool hidden = false;
    bool disabled = false;
    friend constexpr bool operator==(Visibility, Visibility) = default;
};

struct Item {
    ItemKind kind = ItemKind::Unknown;
    ExShift ex = ExShift::None;
    bool hps_marker = false;
    bool declares_page = false;
    SubmenuNo page{};
    SubmenuNo declared_page{};
    BitField bits{};

    std::optional<FileSlotDigit> digit{};
    bool opensave = false;
    bool store_name = false;
    std::uint8_t cond_count = 0;
    std::array<Cond, kMaxConds> conds{};
    TextSpan raw{};
    TextSpan body{};
    std::uint16_t subfield_first = 0;
    std::uint8_t subfield_count = 0;
    friend constexpr bool operator==(const Item&, const Item&) = default;
};

static_assert(std::is_trivially_copyable_v<Item>);

[[nodiscard]] Visibility evaluate(const Item& it, OsdMask hdmask) noexcept;

}
