// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

#include "infra/message_sum.h"
#include "proto/types.h"
#include "ui/config_slots_mode.h"
#include "ui/pair_start.h"

namespace mister::ui {

class PageArg final {
public:
    struct None {
        friend constexpr bool operator==(None, None) = default;
    };

    struct Submenu {
        proto::SubmenuNo submenu{};
        friend constexpr bool operator==(Submenu, Submenu) = default;
    };

    struct SlotsMode {
        ConfigSlotsMode mode = ConfigSlotsMode::Load;
        friend constexpr bool operator==(SlotsMode, SlotsMode) = default;
    };

    struct Slot {
        proto::FileRowId row;
        friend constexpr bool operator==(Slot, Slot) = default;
    };

    struct Pair {
        PairStart start = PairStart::Pick;
        friend constexpr bool operator==(Pair, Pair) = default;
    };

    enum class Kind : std::uint8_t { None, Submenu, SlotsMode, Slot, Pair, kCount };
    using Arm = std::variant<None, Submenu, SlotsMode, Slot, Pair>;
    static_assert(
        std::variant_size_v<Arm> == infra::ordinal(Kind::kCount),
        "PageArg: one Kind per arm"
    );
    template <Kind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<infra::ordinal(K), Arm>, T>;
    static_assert(
        kArmIs<Kind::None, None>
        && kArmIs<Kind::Submenu, Submenu>
        && kArmIs<Kind::SlotsMode, SlotsMode>
        && kArmIs<Kind::Slot, Slot>
        && kArmIs<Kind::Pair, Pair>
    );

    constexpr PageArg() noexcept = default;

    constexpr explicit PageArg(proto::SubmenuNo submenu) noexcept
        : arm_(submenu == proto::kRootSubmenu ? Arm{None{}} : Arm{Submenu{submenu}}) {}

    constexpr explicit PageArg(ConfigSlotsMode mode) noexcept : arm_(SlotsMode{mode}) {}

    constexpr explicit PageArg(proto::FileRowId row) noexcept : arm_(Slot{row}) {}

    constexpr explicit PageArg(PairStart start) noexcept : arm_(Pair{start}) {}

    [[nodiscard]] constexpr bool fits(Kind takes) const noexcept {
        const bool none = std::get_if<None>(&arm_) != nullptr;
        switch (takes) {
            case Kind::None:
                return none;
            case Kind::Submenu:
                return none || std::get_if<Submenu>(&arm_) != nullptr;
            case Kind::SlotsMode:
                return std::get_if<SlotsMode>(&arm_) != nullptr;
            case Kind::Slot:
                return std::get_if<Slot>(&arm_) != nullptr;
            case Kind::Pair:
                return none || std::get_if<Pair>(&arm_) != nullptr;
            case Kind::kCount:
                return false;
        }
        return false;
    }

    [[nodiscard]] constexpr std::optional<proto::SubmenuNo> submenu_or_root() const noexcept {
        if (const Submenu* s = std::get_if<Submenu>(&arm_))
            return s->submenu;
        if (std::get_if<None>(&arm_) != nullptr)
            return proto::kRootSubmenu;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<ConfigSlotsMode> slots_mode() const noexcept {
        if (const SlotsMode* m = std::get_if<SlotsMode>(&arm_))
            return m->mode;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<proto::FileRowId> slot() const noexcept {
        if (const Slot* s = std::get_if<Slot>(&arm_))
            return s->row;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<PairStart> pair_start() const noexcept {
        if (const Pair* p = std::get_if<Pair>(&arm_))
            return p->start;
        if (std::get_if<None>(&arm_) != nullptr)
            return PairStart::Pick;
        return std::nullopt;
    }

    friend constexpr bool operator==(const PageArg&, const PageArg&) = default;

private:
    Arm arm_{};
};

static_assert(std::is_trivially_copyable_v<PageArg>);
static_assert(PageArg{proto::kRootSubmenu} == PageArg{});

}
