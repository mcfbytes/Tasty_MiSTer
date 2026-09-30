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
#include "app/load_window.h"
#include "app/path_text.h"
#include "cores/loader.h"
#include "cores/transfer_row.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/file.h"

namespace mister::app {

class LoadLadder {
    TASTY_SEAT_EXEMPT(main);

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
        Close,
        Cut,
        Over
    };

    struct Host {
        LinkTxChannel& inbox;
        const FileTxLevelCell* level;
        EventQueue& events;
    };

    struct Job {
        std::unique_ptr<cores::ILoader> loader;
        std::unique_ptr<svc::IFile> file;
        std::uint64_t total = 0;
        std::uint8_t wire_index = 0;
        std::uint32_t act = 0;

        cores::TransferRow row{};

        std::optional<LoadWindow> mirror{};
    };

    [[nodiscard]] static Ex<LoadLadder> start(Job job, LoadWindow window, std::string_view path);

    [[nodiscard]] Pass step(Host& h);

    [[nodiscard]] bool wants_pass(const LinkTxChannel& inbox,
                                  const FileTxLevelCell* level) const noexcept;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }
    [[nodiscard]] std::uint64_t written() const noexcept { return off_; }
    [[nodiscard]] const cores::ILoader& loader() const noexcept { return *job_.loader; }

    [[nodiscard]] std::unique_ptr<cores::ILoader> take_loader() && noexcept {
        return std::move(job_.loader);
    }

private:
    LoadLadder(Job job, LoadWindow window) noexcept
        : job_(std::move(job)), window_(std::move(window)) {}

    [[nodiscard]] Pass open_(Host& h);
    [[nodiscard]] Pass await_open_(Host& h);
    [[nodiscard]] Pass chunk_(Host& h);
    [[nodiscard]] Pass fill_(Host& h);
    [[nodiscard]] Pass conclude_(Host& h);
    [[nodiscard]] Pass finish_();
    [[nodiscard]] Pass facts_(Host& h);
    [[nodiscard]] Pass close_(Host& h);
    [[nodiscard]] Pass notify_(Host& h);
    [[nodiscard]] Pass cut_(Host& h);

    void progress_(Host& h, bool edge) noexcept;

    Job job_;
    LoadWindow window_;
    PathText path_{};
    std::vector<std::uint8_t> buf_;
    std::uint64_t off_ = 0;
    std::uint64_t fill_off_ = 0;
    std::uint64_t extent_ = 0;
    Pc pc_ = Pc::Open;
};

}  // namespace mister::app
