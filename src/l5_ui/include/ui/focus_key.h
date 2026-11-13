// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/page_arg.h"
#include "ui/page_id.h"

namespace mister::ui {

class FocusKey final {
public:
    constexpr FocusKey() noexcept = default;

    [[nodiscard]] static constexpr FocusKey of(PageId page, const PageArg& arg) noexcept {
        return FocusKey{page, arg};
    }

    friend constexpr bool operator==(const FocusKey&, const FocusKey&) = default;

private:
    constexpr FocusKey(PageId page, const PageArg& arg) noexcept : page_(page), arg_(arg) {}

    PageId page_ = PageId::None;
    PageArg arg_{};
};

}
