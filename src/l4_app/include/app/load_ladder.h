// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "app/event.h"
#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/ocram_lane_writer.h"
#include "app/load_key.h"
#include "app/load_window.h"
#include "app/types.h"
#include "app/path_text.h"
#include "app/rung.h"
#include "cores/loader.h"
#include "cores/transfer_row.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::app {

class LoadLadder final : public Rung {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Stepped, Waiting, Done, Failed };

    enum class Pc : std::uint8_t {
        Open,
        AwaitOpen,
        Chunk,
        Fill,
        Conclude,
        Finish,
        Facts,
        Mount,
        Close,
        Cut,
        Over,

        LaneOpen,
        LaneAct,
        LaneAnswer,
        LaneEnd
    };

    struct Host {
        LinkTxChannel<SeatTag::Main>& inbox;
        const FileTxLevelCell& level;
        EventQueue<SeatTag::Main>::Producer events;

        infra::OptRef<OcramLaneWriter> lane{};
    };

    struct Job {
        std::unique_ptr<cores::ILoader> loader;
        std::unique_ptr<svc::IFile> file;
        std::uint64_t total = 0;
        proto::WideIoIndex wire_index{};
        proto::ActKey act{};

        cores::TransferRow row{};

        std::optional<LoadWindow<seat_owner::Main>> mirror{};

        ContentEdge edge = ContentEdge::Content;

        PathText save{};
        proto::FileSize save_bytes{};
    };

    [[nodiscard]] static Ex<LoadLadder> start(
        Job job,
        LoadWindow<seat_owner::Main> window,
        std::string_view path
    );

    [[nodiscard]] Pass step(Host& h);

    [[nodiscard]] bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept override;
    [[nodiscard]] Rung::Pass step(const RungEdges& edges, cores::ILadderHost& owner) override;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }

    [[nodiscard]] proto::FileSize written() const noexcept { return off_; }

    [[nodiscard]] const cores::ILoader& loader() const noexcept { return *job_.loader; }

    [[nodiscard]] std::unique_ptr<cores::ILoader> take_loader() && noexcept {
        return std::move(job_.loader);
    }

private:
    LoadLadder(Job job, LoadWindow<seat_owner::Main> window) noexcept
        : job_(std::move(job)),
          window_(std::move(window)),
          key_(job_.total) {}

    [[nodiscard]] Pass open_(Host& h);
    [[nodiscard]] Pass await_open_(Host& h);
    [[nodiscard]] Pass chunk_(Host& h);
    [[nodiscard]] Pass fill_(Host& h);
    [[nodiscard]] Pass conclude_(Host& h);
    [[nodiscard]] Pass finish_();
    [[nodiscard]] Pass facts_(Host& h);
    [[nodiscard]] Pass mount_(Host& h);
    [[nodiscard]] Pass close_(Host& h);
    [[nodiscard]] Pass notify_(Host& h);
    [[nodiscard]] Pass cut_(Host& h);

    [[nodiscard]] Pass lane_step_(Host& h);
    [[nodiscard]] Pc close_pc_() const noexcept;

    void progress_(Host& h, bool edge) noexcept;
    [[nodiscard]] proto::FileSize bracket_length_() const noexcept;

    Job job_;
    LoadWindow<seat_owner::Main> window_;
    LoadKey key_;
    PathText path_{};
    std::vector<std::uint8_t> buf_;
    proto::FileSize off_{};
    proto::FileSize fill_off_{};
    std::uint64_t extent_ = 0;
    bool facts_pushed_ = false;

    bool off_link_ = false;
    bool mounted_ = false;
    bool lane_framed_ = false;
    bool lane_ended_ = false;
    Pc pc_ = Pc::Open;
};

}
