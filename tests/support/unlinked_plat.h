// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "board_parts.h"
#include "boot_parts.h"
#include "hal/board_profile.h"
#include "hal/board_windows.h"
#include "hal/boards_table.h"
#include "app/deadline_miss_log.h"
#include "app/log_lane.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/rt_stats.h"
#include "reactor/executive.h"
#include "reactor/round_timer.h"
#include "rt_evidence.h"
#include "support/de10_row.h"
#include "support/temp_root.h"
#include "svc/vfs.h"

namespace mister::testing {

class UnlinkedPlat {
public:
    explicit UnlinkedPlat(std::string_view stem) : root_{stem} {
        for (const hal::BoardProfile* row : hal::board_table()) {
            if (!row->verified) {
                row_ = row;
                break;
            }
        }
        if (row_ == nullptr || !root_.ok())
            return;
        windows_.emplace(hal::BoardWindows::open(*row_, hal::FallbackNotice::Quiet));
        if (!*windows_)
            return;
        parts_.emplace(fw::BoardParts::make(*row_, **windows_, stats()));
        if (!*parts_)
            return;
        exec_.emplace(
            reactor::Executive::create(
                stats(),
                {.round_timer = infra::OptRef<reactor::RoundTimer>{round_timer()},
                 .miss_log = infra::OptRef<reactor::IDeadlineMissSink>{miss_log_}}
            )
        );
        if (!*exec_)
            return;
        vfs_.emplace(svc::Vfs::create_at(root_.path()));
        if (!*vfs_)
            return;
        fw::BoardParts& p = **parts_;
        plat_.emplace(
            fw::BootParts{
                .link = p.link(),
                .bridges = p.bridges(),
                .programmer = p.programmer(),
                .fpga_mem = row_->fpga_mem,
                .lw_window = hal::lw_region(*row_),
                .regions = row_->regions,
                .doorbells = row_->doorbells,
                .doorbell_nodes = hal::doorbell_nodes(*row_),
                .aperture_node = hal::aperture_node(*row_),
                .video = row_->video,
                .kernel = row_->kernel,
                .threads = row_->threads,
                .exec = **exec_,
                .stats = stats(),
                .round_timer = round_timer(),
                .rt_lane = rt_lane(),
                .vfs = **vfs_,
                .input_dir = input_dir_,
                .rt_evidence = ev_,
                .link_timing = kDe10LinkTiming,
                .program = kDe10ProgramGeometry,
                .cmd_fifo_path = fifo_path_.c_str(),
                .diag_path = diag_path_.c_str(),
                .connect = fw::ConnectPaths{
                    .conf = conf_path_,
                    .net_class = net_path_,
                    .bus_socket = bus_path_
                },
            }
        );
    }

    UnlinkedPlat(const UnlinkedPlat&) = delete;
    UnlinkedPlat& operator=(const UnlinkedPlat&) = delete;

    [[nodiscard]] bool ok() const noexcept { return plat_.has_value(); }

    [[nodiscard]] const fw::BootParts& plat() const noexcept { return *plat_; }

    void rearm_rt_lane_and_timer() const noexcept {
        rt_lane_.reset();
        rt_lane_.emplace();
        round_timer_.reset();
        round_timer_.emplace();
    }

private:
    static xthread::RtStats& stats() {
        static xthread::RtStats s{};
        return s;
    }

    reactor::RoundTimer& round_timer() noexcept { return *round_timer_; }

    app::LogLane& rt_lane() noexcept { return *rt_lane_; }

    mutable std::optional<app::LogLane> rt_lane_{std::in_place};

    app::DeadlineMissLog miss_log_{rt_lane_->producer()};
    mutable std::optional<reactor::RoundTimer> round_timer_{std::in_place};
    const hal::BoardProfile* row_ = nullptr;
    TempRoot root_;
    std::string input_dir_ = root_.path();
    std::string fifo_path_ = root_.path() + "/MiSTer_cmd";
    std::string diag_path_ = root_.path() + "/MiSTer_diag";

    std::string conf_path_ = root_.path() + "/wpa_supplicant.conf";
    std::string net_path_ = root_.path() + "/net";
    std::string bus_path_ = root_.path() + "/no-bus";
    std::optional<Ex<hal::BoardWindows>> windows_{};
    std::optional<std::expected<fw::BoardParts, fw::BoardParts::Refusal>> parts_{};
    std::optional<Ex<reactor::Executive>> exec_{};
    std::optional<Ex<svc::Vfs>> vfs_{};
    fw::RtEvidence ev_{};
    std::optional<const fw::BootParts> plat_{};
};

}
