// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "app/echo_rate.h"
#include "app/echo_wait.h"
#include "app/load_ladder.h"
#include "app/load_window_map.h"
#include "app/rung.h"
#include "cores/core_window_decl.h"
#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "cores/load_plan.h"
#include "cores/loader.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "hal/phys_region.h"
#include "svc/vfs.h"

namespace mister::app {

class LoadWalk final : public Rung {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Stepped, Waiting, Done, Failed };

    enum class Pc : std::uint8_t {
        Hold,
        Row,
        Rung,
        Pieces,
        Echo,
        Notify,
        Save,
        SaveWait,
        Release,
        Over
    };

    static constexpr std::chrono::nanoseconds kSaveBoundNs{5'000'000'000};

    static constexpr EchoRate kEcho{std::chrono::seconds{2}, std::chrono::milliseconds{250}};

    struct Host {
        LoadLadder::Host rung;
        cores::ILadderHost& owner;
        ILoadWindowMap<seat_owner::Main>& map;
        hal::PhysRegion aperture;
        const svc::Vfs& vfs;
        std::chrono::nanoseconds now_ns{};
    };

    [[nodiscard]] static Ex<LoadWalk> start(
        std::unique_ptr<cores::ILoader> loader,
        const cores::LoadPlan& plan,
        std::span<const cores::CoreWindowDecl> windows
    );

    [[nodiscard]] Pass step(Host& h);

    [[nodiscard]] bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept override;
    [[nodiscard]] Rung::Pass step(const RungEdges& edges, cores::ILadderHost& owner) override;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }

    [[nodiscard]] std::size_t row() const noexcept { return row_; }

    [[nodiscard]] bool refused() const noexcept { return plan_.frame.refused; }

    [[nodiscard]] bool stalled() const noexcept { return stalled_; }

private:
    LoadWalk(
        std::unique_ptr<cores::ILoader> loader,
        const cores::LoadPlan& plan,
        std::span<const cores::CoreWindowDecl> windows
    ) noexcept
        : loader_(std::move(loader)),
          plan_(plan),
          windows_(windows) {}

    [[nodiscard]] Pass hold_(Host& h);
    [[nodiscard]] Pass row_step_(Host& h);
    [[nodiscard]] Pass window_(Host& h, const cores::TransferRow& r);
    [[nodiscard]] Pass payload_(Host& h, const cores::TransferRow& r);
    [[nodiscard]] Pass pieces_step_(Host& h);
    [[nodiscard]] Pass send_(
        Host& h,
        std::span<const std::uint8_t> bytes,
        const cores::PostNotify& notify
    );
    [[nodiscard]] Pass notify_(Host& h, const cores::PostNotify& n);
    [[nodiscard]] Pass rung_(Host& h);
    [[nodiscard]] Pass echo_(Host& h);
    [[nodiscard]] Pass save_(Host& h);
    [[nodiscard]] Pass save_wait_(Host& h);
    [[nodiscard]] Pass release_(Host& h);
    [[nodiscard]] Pass fail_(Host& h);
    [[nodiscard]] Pass skip_(const cores::TransferRow& r);
    [[nodiscard]] bool status_bit0_(Host& h, bool asserted);
    void next_row_() noexcept;

    std::unique_ptr<cores::ILoader> loader_;

    const cores::LoadPlan plan_;
    std::span<const cores::CoreWindowDecl> windows_;
    std::optional<LoadLadder> ladder_;
    std::optional<cores::PayloadPieces> pieces_;
    EchoWait echo_wait_{};
    proto::FileSize echo_bytes_{};
    std::size_t row_ = 0;
    proto::ActKey save_gen_{};
    std::chrono::nanoseconds save_due_ns_{};
    bool failed_ = false;
    bool stalled_ = false;
    bool landed_ = false;
    Pc pc_ = Pc::Hold;
};

}
