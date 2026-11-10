// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/link_rx_channel.h"
#include "app/mount_status_cell.h"
#include "app/owed_save_bind.h"
#include "app/path_text.h"
#include "cores/mount_status.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/deadline.h"
#include "proto/types.h"

namespace mister::app {

class OwedMount {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Cell = MountStatusCell;

    OwedMount(Cell& cell, LinkRxChannel<SeatTag::Main>& rx) noexcept : owed_(cell, rx) {}

    OwedMount(const OwedMount&) = delete;
    OwedMount& operator=(const OwedMount&) = delete;
    OwedMount(OwedMount&&) = delete;

    ~OwedMount() { strand(); }

    [[nodiscard]] bool owed() const noexcept { return owed_.owed(); }

    [[nodiscard]] proto::ActKey generation() const noexcept { return owed_.generation(); }

    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }

    [[nodiscard]] proto::SlotIndex slot() const noexcept { return slot_; }

    [[nodiscard]] bool parked() const noexcept { return !quiesce_.has_value(); }

    void mark_parked() noexcept { quiesce_.reset(); }

    [[nodiscard]] std::optional<os::Deadline> quiesce() const noexcept { return quiesce_; }

    void arm(
        proto::ActKey generation,
        const cores::MountStatus& on_strand,
        proto::SlotIndex slot,
        std::string_view path,
        std::optional<os::Deadline> quiesce
    ) noexcept {
        owed_.arm(generation, on_strand);
        slot_ = slot;
        (void)path_.assign(path);
        quiesce_ = quiesce;
    }

    void answer(cores::MountVerdict v, const cores::MountStatus& payload) noexcept {
        if (!owed_.owed())
            return;
        owed_.answer(v, payload);
        forget_();
    }

    void strand() noexcept {
        if (!owed_.owed())
            return;
        owed_.strand();
        forget_();
    }

private:
    void forget_() noexcept {
        slot_ = {};
        path_.clear();
        quiesce_.reset();
    }

    OwedAnswer<Cell, cores::MountVerdict::Failed> owed_;
    FixedStr<kPathMax, StrFit::Clip> path_{};
    std::optional<os::Deadline> quiesce_{};
    proto::SlotIndex slot_{};
};

}
