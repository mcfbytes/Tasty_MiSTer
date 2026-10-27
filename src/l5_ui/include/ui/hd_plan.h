// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "infra/opt_ref.h"
#include "ui/hd_row.h"
#include "ui/page_id.h"

namespace mister::ui {

class IHdRewalk;
struct SystemFacts;

class IHdPlan {
public:
    virtual ~IHdPlan() = default;

    [[nodiscard]] virtual PageId id() const = 0;
    [[nodiscard]] virtual std::string_view title() const = 0;

    [[nodiscard]] virtual std::span<const std::reference_wrapper<IHdRow>> rows() const = 0;

    [[nodiscard]] virtual std::uint8_t pinned() const noexcept { return 0; }

    [[nodiscard]] virtual bool remembers_focus() const noexcept { return true; }

    virtual void refresh(const SystemFacts& facts) { (void)facts; }

    virtual infra::OptRef<IHdRewalk> rewalker() { return {}; }

    [[nodiscard]] virtual std::uint32_t refusals() const noexcept { return 0; }

    [[nodiscard]] virtual bool hub() const noexcept { return false; }

protected:
    IHdPlan() = default;
    IHdPlan(const IHdPlan&) = default;
    IHdPlan& operator=(const IHdPlan&) = default;
};

}
