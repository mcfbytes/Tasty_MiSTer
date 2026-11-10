// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cores/file_slot.h"
#include "cores/types.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/bit_field.h"
#include "proto/types.h"

namespace mister::cores {

inline constexpr std::size_t kMaxOptionRows = 40;

inline constexpr std::size_t kOptionTextChars = 31;

inline constexpr std::size_t kMaxOptionPages = 4;
inline constexpr std::size_t kOptionTitleChars = 20;

struct OptionRow {
    TASTY_SEAT_EXEMPT(const_shared);

    enum class Link : std::uint8_t { None, SubPage, LoadConfig, SaveConfig, Mt32Pi, kCount };

    enum class Host : std::uint8_t { None, JoySwap, kCount };

    enum class ResetAct : std::uint8_t { None, Warm, Cold, kCount };

    struct CycleArm {
        WordId word{};
        proto::BitField bits{};
        std::span<const std::string_view> values{};
        ChoiceCount choices{};
        bool arms_reset = false;

        [[nodiscard]] constexpr proto::FieldCount choice_count() const noexcept {
            return choices.reach(proto::FieldCount::of(values.size()));
        }

        [[nodiscard]] constexpr std::string_view value_name(proto::FieldValue pick) const noexcept {
            if (!pick.within(proto::FieldCount::of(values.size())))
                return {};
            return infra::at_index(values, pick);
        }
    };

    struct FileArm {
        IoIndex slot{};
        bool ejects_on_select = false;
        bool browse_unmount = false;
    };

    struct ActionArm {
        ResetAct reset_act = ResetAct::None;
    };

    struct LinkArm {
        Link link = Link::None;
        proto::SubmenuNo link_page{};
    };

    struct HostArm {
        Host host = Host::None;
        std::span<const std::string_view> values{};
    };

    struct SeparatorArm {};

    enum class Kind : std::uint8_t { Cycle, File, Action, Link, Host, Separator, kCount };
    using Arm = std::variant<CycleArm, FileArm, ActionArm, LinkArm, HostArm, SeparatorArm>;
    static_assert(
        std::variant_size_v<Arm> == static_cast<std::size_t>(Kind::kCount),
        "OptionRow: one Kind name per arm"
    );
    template <Kind K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Arm>, T>;
    static_assert(
        kArmIs<Kind::Cycle, CycleArm>
        && kArmIs<Kind::File, FileArm>
        && kArmIs<Kind::Action, ActionArm>
        && kArmIs<Kind::Link, LinkArm>
        && kArmIs<Kind::Host, HostArm>
        && kArmIs<Kind::Separator, SeparatorArm>
    );

    std::string_view label{};
    proto::SubmenuNo page{};
    OptionTag id{};

    Arm arm = SeparatorArm{};

    [[nodiscard]] constexpr Kind kind() const noexcept { return static_cast<Kind>(arm.index()); }

    [[nodiscard]] constexpr const CycleArm* cycle_arm() const noexcept {
        return std::get_if<CycleArm>(&arm);
    }

    [[nodiscard]] constexpr const FileArm* file_arm() const noexcept {
        return std::get_if<FileArm>(&arm);
    }

    [[nodiscard]] constexpr const ActionArm* action_arm() const noexcept {
        return std::get_if<ActionArm>(&arm);
    }

    [[nodiscard]] constexpr const LinkArm* link_arm() const noexcept {
        return std::get_if<LinkArm>(&arm);
    }

    [[nodiscard]] constexpr const HostArm* host_arm() const noexcept {
        return std::get_if<HostArm>(&arm);
    }

    [[nodiscard]] static constexpr OptionRow cycle(
        std::string_view label,
        proto::SubmenuNo page,
        OptionTag id,
        CycleArm arm
    ) noexcept {
        return headed(label, page, id, arm);
    }

    [[nodiscard]] static constexpr OptionRow file(
        std::string_view label,
        proto::SubmenuNo page,
        OptionTag id,
        FileArm arm
    ) noexcept {
        return headed(label, page, id, arm);
    }

    [[nodiscard]] static constexpr OptionRow action(
        std::string_view label,
        proto::SubmenuNo page,
        OptionTag id,
        ActionArm arm
    ) noexcept {
        return headed(label, page, id, arm);
    }

    [[nodiscard]] static constexpr OptionRow to(
        std::string_view label,
        proto::SubmenuNo page,
        OptionTag id,
        LinkArm arm
    ) noexcept {
        return headed(label, page, id, arm);
    }

    [[nodiscard]] static constexpr OptionRow host_fact(
        std::string_view label,
        proto::SubmenuNo page,
        OptionTag id,
        HostArm arm
    ) noexcept {
        return headed(label, page, id, arm);
    }

    [[nodiscard]] static constexpr OptionRow separator(proto::SubmenuNo page) noexcept {
        return headed({}, page, OptionTag{}, SeparatorArm{});
    }

private:
    [[nodiscard]] static constexpr OptionRow headed(
        std::string_view label,
        proto::SubmenuNo page,
        OptionTag id,
        Arm arm
    ) noexcept {
        OptionRow r{};
        r.label = label;
        r.page = page;
        r.id = id;
        r.arm = arm;
        return r;
    }
};

}
