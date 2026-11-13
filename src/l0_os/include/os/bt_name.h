// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>
#include <type_traits>

#include "infra/fixed_str.h"
#include "infra/utf8.h"

namespace mister::os {

class BtName {
public:
    static constexpr std::size_t kCapacity = 63;

    [[nodiscard]] static BtName of(std::string_view name) noexcept {
        BtName out;
        (void)out.text_.assign(infra::utf8_prefix(name, kCapacity));
        return out;
    }

    [[nodiscard]] std::string_view view() const noexcept { return text_.view(); }

    [[nodiscard]] bool empty() const noexcept { return text_.empty(); }

    friend constexpr bool operator==(const BtName& a, const BtName& b) noexcept {
        return a.text_ == b.text_;
    }

private:
    FixedStr<kCapacity + 1, StrFit::Clip> text_{};
};

static_assert(std::is_trivially_copyable_v<BtName>);
static_assert(std::has_unique_object_representations_v<BtName>);

}
