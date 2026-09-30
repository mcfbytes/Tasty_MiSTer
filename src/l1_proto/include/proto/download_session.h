// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::proto {

class DownloadSession {
    TASTY_SEAT_RESIDENT(RT);

public:
    static Ex<DownloadSession> begin(hal::ISpiTransport& link, IoIndex index);

    static Ex<DownloadSession> begin(hal::ISpiTransport& link, IoIndex index,
                                     const SessionParams& params);
    static Ex<DownloadSession> begin(hal::ISpiTransport& link, WideIoIndex index,
                                     const SessionParams& params = {});

    [[nodiscard]] static Ex<DownloadSession> open(hal::ISpiTransport& link, WideIoIndex index);

    ~DownloadSession();
    DownloadSession(DownloadSession&& o) noexcept;
    DownloadSession& operator=(DownloadSession&&) = delete;

    Ex<void> write(std::span<const std::uint8_t> data);
    Ex<void> write(std::span<const std::uint8_t> data, hal::Width w);
    Ex<void> read(std::span<std::uint8_t> data);
    Ex<void> read(std::span<std::uint8_t> data, hal::Width w);

    Ex<void> end();

    [[nodiscard]] Ex<void> post(std::span<const std::uint16_t> words);

    [[nodiscard]] Ex<bool> settle(std::span<const std::uint16_t> tail);

    void abandon() noexcept;
    [[nodiscard]] bool posted() const noexcept { return posted_; }

    static Ex<void> set_index(hal::ISpiTransport& link, IoIndex index);
    static Ex<void> set_index(hal::ISpiTransport& link, WideIoIndex index);

    static Ex<void> send_file_info(hal::ISpiTransport& link, std::string_view ext);

    static Ex<void> send_cheats(hal::ISpiTransport& link, std::span<const std::uint8_t> table);

    bool active() const noexcept { return link_ != nullptr; }
    TransferDirection direction() const noexcept { return direction_; }
    WideIoIndex index() const noexcept { return WideIoIndex{index_}; }

private:
    explicit DownloadSession(hal::ISpiTransport& link) : link_(&link) {}
    hal::ISpiTransport* link_;
    std::uint16_t index_ = 0;
    TransferDirection direction_ = TransferDirection::Download;
    bool posted_ = false;
};

}  // namespace mister::proto
