// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/atari8/atari8_address.h"
#include "cores/atari8/atari8_fill.h"
#include "cores/atari8/atari8_status.h"
#include "cores/atari8/sio_types.h"
#include "infra/byte_count.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::atari8 {

struct HostPoke {
    Atari8Address at;
    std::span<const std::byte> bytes;
};

class XexFeed {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;

    enum class Step : std::uint8_t {
        Closed,
        Booting,
        Rebooting,
        Arm,
        Clear,
        Loader,
        Vectors,
        Probe,
        Patch,
        Release,
        Poll,
        InitVector,
        RunVector,
        Segment,
        Next,
        End,
    };

    void close() noexcept;

    void open(std::unique_ptr<svc::IFile> file) noexcept;

    void boot_ordered() noexcept;
    void booted() noexcept;
    [[nodiscard]] bool wants_boot() const noexcept;
    [[nodiscard]] bool rebooting() const noexcept;

    [[nodiscard]] bool take_release() noexcept;

    [[nodiscard]] bool arming() const noexcept;

    [[nodiscard]] bool wants() const noexcept;
    [[nodiscard]] proto::MailboxAct next(ActBytes bytes);
    void settled(const proto::MailboxResult& result, ActBytes bytes);

    [[nodiscard]] Step step() const noexcept;

    static constexpr infra::ByteOffset kStatusAt{0x0E};
    static constexpr infra::ByteCount kPollBytes{kStatusAt.v + 1};

private:
    void read_start_() noexcept;
    void read_end_() noexcept;
    [[nodiscard]] std::optional<HostAddress> read_pair_() noexcept;
    void end_() noexcept;
    void patches_(bool brians) noexcept;

    Step step_ = Step::Closed;
    std::unique_ptr<svc::IFile> file_{};
    proto::FileOffset at_{};
    bool first_ = true;
    bool released_ = false;
    Atari8Status status_{};
    Atari8Fill clear_{};
    std::span<const HostPoke> pokes_{};
    HostAddress start_{};
    HostAddress load_{};
    infra::ByteCount left_{};
};

}
