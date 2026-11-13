// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>
#include <variant>

#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "ui/page_arg.h"
#include "ui/page_id.h"

namespace mister::ui {

class Transition final {
public:
    struct Stay {
        friend constexpr bool operator==(Stay, Stay) = default;
    };

    struct Push {
        PageId page = PageId::None;
        PageArg arg{};
        friend constexpr bool operator==(Push, Push) = default;
    };

    struct Pop {
        friend constexpr bool operator==(Pop, Pop) = default;
    };

    struct Replace {
        PageId page = PageId::None;
        PageArg arg{};
        friend constexpr bool operator==(Replace, Replace) = default;
    };

    struct CloseAll {
        friend constexpr bool operator==(CloseAll, CloseAll) = default;
    };

    struct Leave {
        friend constexpr bool operator==(Leave, Leave) = default;
    };

    struct ChooseHdStyle {
        friend constexpr bool operator==(ChooseHdStyle, ChooseHdStyle) = default;
    };

    struct ChooseClassicStyle {
        friend constexpr bool operator==(ChooseClassicStyle, ChooseClassicStyle) = default;
    };

    enum class Kind : std::uint8_t {
        Stay,
        Push,
        Pop,
        Replace,
        CloseAll,
        Leave,
        ChooseHdStyle,
        ChooseClassicStyle,
        kCount
    };
    using Arm = std::
        variant<Stay, Push, Pop, Replace, CloseAll, Leave, ChooseHdStyle, ChooseClassicStyle>;
    static_assert(
        std::variant_size_v<Arm> == infra::ordinal(Kind::kCount),
        "Transition: one Kind per arm"
    );
    template <Kind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<infra::ordinal(K), Arm>, T>;
    static_assert(
        kArmIs<Kind::Stay, Stay>
        && kArmIs<Kind::Push, Push>
        && kArmIs<Kind::Pop, Pop>
        && kArmIs<Kind::Replace, Replace>
        && kArmIs<Kind::CloseAll, CloseAll>
        && kArmIs<Kind::Leave, Leave>
        && kArmIs<Kind::ChooseHdStyle, ChooseHdStyle>
        && kArmIs<Kind::ChooseClassicStyle, ChooseClassicStyle>
    );

    constexpr Transition() noexcept = default;

    [[nodiscard]] static constexpr Transition push(PageId page, PageArg arg = PageArg{}) noexcept {
        return Transition{Arm{Push{page, arg}}};
    }

    [[nodiscard]] static constexpr Transition replace(
        PageId page,
        PageArg arg = PageArg{}
    ) noexcept {
        return Transition{Arm{Replace{page, arg}}};
    }

    [[nodiscard]] static constexpr Transition pop() noexcept { return Transition{Arm{Pop{}}}; }

    [[nodiscard]] static constexpr Transition close_all() noexcept {
        return Transition{Arm{CloseAll{}}};
    }

    [[nodiscard]] static constexpr Transition leave() noexcept { return Transition{Arm{Leave{}}}; }

    [[nodiscard]] static constexpr Transition choose_hd_style() noexcept {
        return Transition{Arm{ChooseHdStyle{}}};
    }

    [[nodiscard]] static constexpr Transition choose_classic_style() noexcept {
        return Transition{Arm{ChooseClassicStyle{}}};
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return static_cast<Kind>(arm_.index()); }

    [[nodiscard]] constexpr infra::OptRef<const Push> as_push() const noexcept {
        return infra::opt_ref_of(std::get_if<Push>(&arm_));
    }

    [[nodiscard]] constexpr infra::OptRef<const Replace> as_replace() const noexcept {
        return infra::opt_ref_of(std::get_if<Replace>(&arm_));
    }

    friend constexpr bool operator==(const Transition&, const Transition&) = default;

private:
    constexpr explicit Transition(Arm arm) noexcept : arm_(arm) {}

    Arm arm_{};
};

static_assert(std::is_trivially_copyable_v<Transition>);

}
