// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/reg_word.h"
#include "infra/seat.h"
#include "infra/prelude.h"
#include "infra/unique_fd.h"
#include "infra/opt_ref.h"
#include "infra/seat_owner_io.h"
#include "os/epoll_set.h"
#include "os/uio_handle.h"
#include "os/uio_line_ref.h"
#include "proto/types.h"

namespace mister::app {

class LwSectorEndpoint;
class LwSysEndpoint;

template <class Owner>
class BlockLine {
    TASTY_SEAT_FROM(Owner);

public:
    BlockLine() = default;

    ~BlockLine() { close(); }

    BlockLine(const BlockLine&) = delete;
    BlockLine& operator=(const BlockLine&) = delete;

private:
    friend class LwSectorEndpoint;
    friend class LwSysEndpoint;

    [[nodiscard]] bool open(
        os::UioLineRef from,
        infra::OptRef<const os::EpollSet> doorbells
    ) noexcept;

    void close() noexcept;

    [[nodiscard]] bool rang() const noexcept;

    template <class Port, class Reg>
    [[nodiscard]] proto::CauseBits drain(bool rang, Port& port, Reg cause) noexcept {
        TASTY_SEAT_BODY(SeatOwner);
        if (rang)
            (void)line_.borrow().consume();
        const hal::RegWord w = port.read(cause);
        port.write(cause, w);
        if (rang)
            (void)line_.borrow().rearm();
        return proto::CauseBits::of(w);
    }

    os::UioHandle line_{};
    infra::OptRef<const os::EpollSet> doorbells_{};
};

extern template class BlockLine<seat_owner::Io>;

}
