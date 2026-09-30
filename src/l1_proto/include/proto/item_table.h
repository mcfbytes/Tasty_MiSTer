// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "infra/error.h"
#include "proto/bit_field.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

struct TextSpan {
    std::uint16_t off = 0;
    std::uint16_t len = 0;
    friend constexpr bool operator==(TextSpan, TextSpan) = default;
};

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

enum class CondEffect : std::uint8_t { Hide, Disable };

struct Cond {
    CondEffect effect = CondEffect::Hide;
    bool acts_when_set = false;
    std::uint8_t bit = 0;
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
    std::uint8_t page = 0;
    std::uint8_t declared_page = 0;
    BitField bits{};
    std::uint8_t digit = 0;
    bool has_digit = false;
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

class ItemTable {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<ItemTable> parse(std::string_view raw);

    [[nodiscard]] std::span<const Item> items() const noexcept { return items_; }
    [[nodiscard]] std::string_view core_name() const noexcept { return text(name_); }
    [[nodiscard]] std::string_view text(TextSpan s) const noexcept {
        return (static_cast<std::size_t>(s.off) + s.len <= text_.size())
                   ? std::string_view{text_}.substr(s.off, s.len)
                   : std::string_view{};
    }

    [[nodiscard]] std::string_view subfield(const Item& it, unsigned n) const noexcept;

    [[nodiscard]] std::uint32_t cond_overflows() const noexcept { return cond_overflows_; }

    [[nodiscard]] bool terminated_early() const noexcept { return terminated_early_; }

    friend bool operator==(const ItemTable&, const ItemTable&) = default;

private:
    std::string text_;
    TextSpan name_{};
    std::vector<Item> items_;
    std::vector<TextSpan> spans_;
    std::uint32_t cond_overflows_ = 0;
    bool terminated_early_ = false;
};

}  // namespace mister::proto
