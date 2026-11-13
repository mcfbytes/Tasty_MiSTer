// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "proto/item.h"
#include "proto/text_span.h"
#include "proto/types.h"

namespace mister::proto {

class ItemTable {

public:
    [[nodiscard]] static Ex<ItemTable> parse(std::string_view raw);

    [[nodiscard]] static constexpr std::size_t index_of(ItemOrdinal item) noexcept {
        return item.v;
    }

    [[nodiscard]] std::span<const Item> items() const noexcept { return items_; }

    [[nodiscard]] std::string_view core_name() const noexcept { return text(name_); }

    [[nodiscard]] std::string_view text(TextSpan s) const noexcept {
        return (static_cast<std::size_t>(s.off) + s.len <= text_.size())
            ? std::string_view{text_}.substr(s.off, s.len)
            : std::string_view{};
    }

    [[nodiscard]] std::string_view subfield(const Item& it, unsigned n) const noexcept;

    [[nodiscard]] std::optional<ItemOrdinal> info_row() const noexcept {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const std::string_view raw = text(items_[i].raw);
            if (!raw.empty() && raw.front() == 'I')
                return ItemOrdinal{static_cast<std::uint16_t>(i)};
        }
        return std::nullopt;
    }

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

}
