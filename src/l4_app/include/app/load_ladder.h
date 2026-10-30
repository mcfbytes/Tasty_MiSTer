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
#include "app/load_key.h"
#include "app/load_window.h"
#include "app/path_text.h"
#include "app/rung.h"
#include "cores/loader.h"
#include "cores/transfer_row.h"
#include "infra/error.h"
#include "infra/seat.h"
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
        Over
    };

    struct Host {
        LinkTxChannel<SeatTag::Main>& inbox;
        const FileTxLevelCell* level;
        EventQueue<SeatTag::Main>::Producer events;
    };

    struct Job {
        std::unique_ptr<cores::ILoader> loader;
        std::unique_ptr<svc::IFile> file;
        std::uint64_t total = 0;
        proto::WideIoIndex wire_index{};
        std::uint32_t act = 0;

        cores::TransferRow row{};

        std::optional<LoadWindow<seat_owner::Main>> mirror{};

        PathText save{};
        std::uint64_t save_bytes = 0;
    };

    [[nodiscard]] static Ex<LoadLadder> start(
        Job job,
        LoadWindow<seat_owner::Main> window,
        std::string_view path
    );

    [[nodiscard]] Pass step(Host& h);

    [[nodiscard]] bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell* level
    ) const noexcept;

    [[nodiscard]] bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept override;
    [[nodiscard]] Rung::Pass step(const RungEdges& edges, cores::ILadderHost& owner) override;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }

    [[nodiscard]] std::uint64_t written() const noexcept { return off_; }

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

    void progress_(Host& h, bool edge) noexcept;
    [[nodiscard]] std::uint32_t bracket_length_() const noexcept;

    Job job_;
    LoadWindow<seat_owner::Main> window_;
    LoadKey key_;
    PathText path_{};
    std::vector<std::uint8_t> buf_;
    std::uint64_t off_ = 0;
    std::uint64_t fill_off_ = 0;
    std::uint64_t extent_ = 0;
    Pc pc_ = Pc::Open;
};

}
