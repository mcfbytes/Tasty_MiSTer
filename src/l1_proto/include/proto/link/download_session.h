// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/prelude.h"
#include "hal/spi_transport.h"
#include "proto/link/fio_bracket.h"
#include "proto/link_frame.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::proto::link {

class DownloadSession {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<DownloadSession> begin(hal::ISpiTransport& link, IoIndex index);

    [[nodiscard]] static Ex<DownloadSession> begin(
        hal::ISpiTransport& link,
        IoIndex index,
        const SessionParams& params
    );
    [[nodiscard]] static Ex<DownloadSession> begin(
        hal::ISpiTransport& link,
        WideIoIndex index,
        const SessionParams& params = {}
    );

    [[nodiscard]] static Ex<DownloadSession> open(hal::ISpiTransport& link, WideIoIndex index);

    [[nodiscard]] static Ex<DownloadSession> open(
        hal::ISpiTransport& link,
        WideIoIndex index,
        AuxValue address
    );

    ~DownloadSession();

    DownloadSession(DownloadSession&& o) noexcept
        : bracket_(std::move(o.bracket_)),
          posted_(std::exchange(o.posted_, false)),
          dat_(o.dat_),
          dat_held_(std::exchange(o.dat_held_, false)) {}

    DownloadSession& operator=(DownloadSession&&) = delete;

    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data);
    [[nodiscard]] Ex<void> write(std::span<const std::uint8_t> data, hal::Width w);

    [[nodiscard]] Ex<std::size_t> write_taken(std::span<const std::uint8_t> data);

    [[nodiscard]] Ex<void> end();

    [[nodiscard]] Ex<bool> end_or_post();

    [[nodiscard]] Ex<void> post(std::span<const hal::LinkWord> words);

    [[nodiscard]] Ex<bool> settle(std::span<const hal::LinkWord> tail);

    void abandon() noexcept;

    [[nodiscard]] bool posted() const noexcept { return posted_; }

    [[nodiscard]] Ex<void> open_dat(hal::Width w);
    [[nodiscard]] Ex<void> feed(std::span<const std::uint8_t> data);
    void release_dat() noexcept;

    [[nodiscard]] bool dat_held() const noexcept { return dat_held_; }

    [[nodiscard]] static Ex<void> set_index(ILinkFrame& frame, FioIndexByte index);
    [[nodiscard]] static Ex<void> set_index(ILinkFrame& frame, WideIoIndex index);

    [[nodiscard]] static Ex<void> send_file_info(ILinkFrame& frame, std::string_view ext);

    [[nodiscard]] static Ex<void> send_cheats(
        hal::ISpiTransport& link,
        std::span<const std::uint8_t> table
    );

    bool active() const noexcept { return bracket_.active(); }

    WideIoIndex index() const noexcept { return bracket_.index(); }

private:
    explicit DownloadSession(FioBracket&& b) noexcept : bracket_(std::move(b)) {}

    FioBracket bracket_;
    bool posted_ = false;
    hal::Width dat_ = hal::Width::Byte;
    bool dat_held_ = false;
};

}
