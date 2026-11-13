// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "infra/opt_ref.h"
#include "ui/connect_demand.h"
#include "ui/types.h"
#include "ui/hd_row.h"
#include "ui/page_id.h"
#include "svc/types.h"

namespace mister::ui {

class IHdRewalk;
struct SystemFacts;
struct SessionFacts;

class IHdPlan {
public:
    virtual ~IHdPlan() = default;

    [[nodiscard]] virtual PageId id() const = 0;
    [[nodiscard]] virtual std::string_view title() const = 0;

    [[nodiscard]] virtual std::span<const std::reference_wrapper<IHdRow>> rows() const = 0;

    [[nodiscard]] virtual RowCount pinned() const noexcept { return RowCount{}; }

    [[nodiscard]] virtual bool remembers_focus() const noexcept { return true; }

    virtual void refresh(const SystemFacts& facts, const SessionFacts& session) {
        (void)facts;
        (void)session;
    }

    virtual infra::OptRef<IHdRewalk> rewalker() { return {}; }

    [[nodiscard]] virtual svc::Tally refusals() const noexcept { return svc::Tally{}; }

    [[nodiscard]] virtual ConnectDemand connect_demand() const { return ConnectDemand{}; }

    virtual void on_focus_moved() {}

    [[nodiscard]] virtual bool holds_pairing() const { return false; }

    [[nodiscard]] virtual bool hub() const noexcept { return false; }

protected:
    IHdPlan() = default;
    IHdPlan(const IHdPlan&) = default;
    IHdPlan& operator=(const IHdPlan&) = default;
};

}
