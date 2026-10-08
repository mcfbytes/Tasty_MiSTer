// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/fio_bracket.h"
#include "proto/types.h"

namespace mister::proto {

class UploadSession {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<UploadSession> begin(hal::ISpiTransport& link, WideIoIndex index);

    UploadSession(UploadSession&&) noexcept = default;
    UploadSession& operator=(UploadSession&&) = delete;

    [[nodiscard]] Ex<void> read(std::span<std::uint8_t> data);
    [[nodiscard]] Ex<void> read(std::span<std::uint8_t> data, hal::Width w);
    [[nodiscard]] Ex<void> end() { return bracket_.end(); }

    bool active() const noexcept { return bracket_.active(); }
    WideIoIndex index() const noexcept { return WideIoIndex{bracket_.index()}; }

private:
    explicit UploadSession(FioBracket&& b) noexcept : bracket_(std::move(b)) {}
    FioBracket bracket_;
};

}  // namespace mister::proto
