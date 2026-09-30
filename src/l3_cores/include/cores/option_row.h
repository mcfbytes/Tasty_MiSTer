// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/file_slot.h"
#include "cores/types.h"
#include "infra/seat.h"
#include "proto/bit_field.h"

namespace mister::cores {

inline constexpr std::size_t kMaxOptionRows = 40;

inline constexpr std::size_t kOptionTextChars = 31;

inline constexpr std::size_t kMaxOptionPages = 4;
inline constexpr std::size_t kOptionTitleChars = 20;

struct OptionRow {
    TASTY_SEAT_EXEMPT(const_shared);
    enum class Kind : std::uint8_t { Cycle, File, Action, Link, Host, Separator, kCount };

    enum class Link : std::uint8_t { None, SubPage, LoadConfig, SaveConfig, Mt32Pi, kCount };

    enum class Host : std::uint8_t { None, JoySwap, kCount };

    std::string_view label{};
    std::uint8_t page = 0;
    Kind kind = Kind::Separator;
    std::uint8_t id = 0;

    WordId word{};
    proto::BitField bits{};
    std::span<const std::string_view> values{};
    std::uint8_t choices = 0;
    bool arms_reset = false;

    IoIndex slot{};
    bool ejects_on_select = false;
    bool browse_unmount = false;

    Link link = Link::None;
    std::uint8_t link_page = 0;

    Host host = Host::None;

    [[nodiscard]] constexpr std::uint8_t choice_count() const noexcept {
        return choices != 0 ? choices : static_cast<std::uint8_t>(values.size());
    }

    [[nodiscard]] static constexpr OptionRow cycle(std::string_view label, std::uint8_t page,
                                                   std::uint8_t id, WordId word,
                                                   proto::BitField bits,
                                                   std::span<const std::string_view> values,
                                                   std::uint8_t choices = 0,
                                                   bool arms_reset = false) noexcept {
        OptionRow r{};
        r.label = label;
        r.page = page;
        r.kind = Kind::Cycle;
        r.id = id;
        r.word = word;
        r.bits = bits;
        r.values = values;
        r.choices = choices;
        r.arms_reset = arms_reset;
        return r;
    }
    [[nodiscard]] static constexpr OptionRow file(std::string_view label, std::uint8_t page,
                                                  std::uint8_t id, IoIndex slot,
                                                  bool ejects_on_select,
                                                  bool browse_unmount = false) noexcept {
        OptionRow r{};
        r.label = label;
        r.page = page;
        r.kind = Kind::File;
        r.id = id;
        r.slot = slot;
        r.ejects_on_select = ejects_on_select;
        r.browse_unmount = browse_unmount;
        return r;
    }
    [[nodiscard]] static constexpr OptionRow action(std::string_view label, std::uint8_t page,
                                                    std::uint8_t id) noexcept {
        OptionRow r{};
        r.label = label;
        r.page = page;
        r.kind = Kind::Action;
        r.id = id;
        return r;
    }
    [[nodiscard]] static constexpr OptionRow to(std::string_view label, std::uint8_t page,
                                                std::uint8_t id, Link link,
                                                std::uint8_t link_page = 0) noexcept {
        OptionRow r{};
        r.label = label;
        r.page = page;
        r.kind = Kind::Link;
        r.id = id;
        r.link = link;
        r.link_page = link_page;
        return r;
    }
    [[nodiscard]] static constexpr OptionRow host_fact(
        std::string_view label, std::uint8_t page, std::uint8_t id, Host host,
        std::span<const std::string_view> values) noexcept {
        OptionRow r{};
        r.label = label;
        r.page = page;
        r.kind = Kind::Host;
        r.id = id;
        r.host = host;
        r.values = values;
        return r;
    }
    [[nodiscard]] static constexpr OptionRow separator(std::uint8_t page) noexcept {
        OptionRow r{};
        r.page = page;
        return r;
    }
};

}  // namespace mister::cores
