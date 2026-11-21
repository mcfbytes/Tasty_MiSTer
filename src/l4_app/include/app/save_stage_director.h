// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/save_director_status.h"
#include "infra/indexed_array.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "os/clock.h"
#include "proto/autosave_policy.h"
#include "proto/reset_edge.h"
#include "proto/save_stage.h"
#include "proto/types.h"
#include "svc/save_stage_crossings.h"
#include "svc/types.h"

namespace mister::app {

class SaveStageDirector final {
    TASTY_SEAT_RESIDENT(Main);

public:
    using TimePoint = os::RawMonotonicClock::time_point;

    struct Ends {
        svc::SaveStageOrders& orders;
        const svc::SaveStageCell& report;
    };

    struct Facts {

        struct ProbeFacts {
            proto::BindGeneration conf_gen;
            proto::ResetEdge edge;
            bool usable = false;
            bool menu_focus = false;
        };

        bool enabled = false;
        bool declared = false;

        std::optional<ProbeFacts> probe{};
    };

    enum class Cause : std::uint8_t { Confirmed, Direct, Deferred, Forced };

    struct Tally {
        infra::Tally orders_refused{};
        infra::Tally identical{};
        infra::Tally report_refusals{};
        infra::Tally windows_bounded{};
    };

    SaveStageDirector(const Ends& ends, const os::IClock& clock, SaveDirectorCell& status) noexcept
        : orders_(ends.orders),
          report_cell_(ends.report),
          clock_(clock),
          status_cell_(status) {}

    SaveStageDirector(const SaveStageDirector&) = delete;
    SaveStageDirector& operator=(const SaveStageDirector&) = delete;
    SaveStageDirector(SaveStageDirector&&) = delete;
    SaveStageDirector& operator=(SaveStageDirector&&) = delete;
    ~SaveStageDirector() = default;

    [[nodiscard]] std::optional<proto::ResetEdge> step(const Facts& facts) noexcept;

    [[nodiscard]] bool open_window() noexcept;
    void cancel_window() noexcept;

    void release_holds() noexcept;

    [[nodiscard]] bool order_commit_all(proto::CommitAllSeq seq) noexcept;

    [[nodiscard]] bool order_first_commits() noexcept;
    [[nodiscard]] bool commit_all_served(proto::CommitAllSeq seq) const noexcept;

    [[nodiscard]] std::optional<infra::ParkTimeout> next_due() const noexcept;

    [[nodiscard]] bool window_open() const noexcept { return window_opened_.has_value(); }

    [[nodiscard]] const SaveDirectorStatus& status() const noexcept { return status_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    struct Ordered {
        proto::StageRevision rev{};
        Cause cause = Cause::Direct;
        infra::Tally commits{};
        infra::Tally faults{};
        infra::Tally declines{};
    };

    struct StageWatch {
        proto::StageKey key{};
        infra::Tally commits_base{};
        std::optional<proto::StageRevision> at_open{};
        std::optional<proto::StageRevision> held{};
        bool was_held = false;
        std::optional<TimePoint> unsaved_since{};
        std::optional<TimePoint> retry_at{};
        std::optional<Ordered> ordered{};
        bool in_episode = false;
    };

    struct Stage {
        proto::SaveStageRow row{};
        StageWatch watch{};
    };

    using Stages = infra::IndexedArray<Stage, proto::kMaxStagedSaves.size(), proto::SaveStageSlot>;

    [[nodiscard]] bool read_() noexcept;
    void rows_() noexcept;
    void results_(TimePoint now) noexcept;
    void close_window_(TimePoint now) noexcept;
    void passes_outside_() noexcept;
    void stamp_unsaved_(TimePoint now) noexcept;
    void order_unsaved_(TimePoint now, const Facts& facts) noexcept;
    void order_(
        StageWatch& w,
        const proto::SaveStageRow& row,
        Cause cause,
        const Facts& facts
    ) noexcept;
    [[nodiscard]] static proto::SaveBackup backup_for_(
        const StageWatch& w,
        const proto::SaveStageRow& row,
        const Facts& facts
    ) noexcept;
    void publish_(const Facts& facts) noexcept;
    [[nodiscard]] std::optional<proto::ResetEdge> probe_due_(
        TimePoint now,
        const Facts& facts
    ) noexcept;
    void count_(Cause cause) noexcept;

    svc::SaveStageOrders& orders_;
    const svc::SaveStageCell& report_cell_;
    const os::IClock& clock_;
    SaveDirectorCell& status_cell_;
    Facts facts_{};
    Stages stages_{};
    proto::SaveStageReport::Summary summary_{};
    std::optional<TimePoint> window_opened_{};
    std::optional<TimePoint> last_close_{};
    SaveDirectorStatus status_{};
    std::optional<SaveDirectorStatus> published_{};
    Tally tally_{};
    std::optional<TimePoint> due_{};
    proto::BindGeneration seen_gen_{};
    bool enabled_ = false;
};

}
