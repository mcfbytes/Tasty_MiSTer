// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/dbus_auth.h"
#include "os/dbus_header.h"
#include "os/dbus_incoming.h"
#include "os/dbus_message.h"
#include "os/dbus_serial.h"
#include "os/types.h"
#include "os/unix_stream_socket.h"

namespace mister::os {

class DbusConnection {
    TASTY_SEAT_RESIDENT(Connect);

public:
    static constexpr std::string_view kSystemBusSocket = "/run/dbus/system_bus_socket";

    enum : std::size_t { kInboundArena = 256 * 1024, kOutboundArena = 16 * 1024 };

    static constexpr WireBytes kMaxMessage{DbusHeader::kMaxMessage};

    struct DiscardedFrames {
        std::size_t v = 0;

        [[nodiscard]] constexpr DiscardedFrames next() const noexcept {
            return DiscardedFrames{v + 1};
        }

        friend constexpr bool operator==(DiscardedFrames, DiscardedFrames) = default;
    };

    [[nodiscard]] static Ex<std::optional<DbusConnection>> open(
        std::string_view socket_path = kSystemBusSocket
    ) noexcept;

    DbusConnection(DbusConnection&&) noexcept = default;
    DbusConnection& operator=(DbusConnection&&) noexcept = delete;
    DbusConnection(const DbusConnection&) = delete;
    DbusConnection& operator=(const DbusConnection&) = delete;
    ~DbusConnection() = default;

    [[nodiscard]] Ex<DbusSerial> call(const DbusMessage& message) noexcept;

    [[nodiscard]] Ex<void> reply(const DbusIncoming& request, const DbusMessage& message) noexcept;

    [[nodiscard]] Ex<std::optional<DbusIncoming>> poll() noexcept;

    [[nodiscard]] const UniqueFd* fd() const noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    [[nodiscard]] bool closed() const noexcept { return closed_; }

    [[nodiscard]] DiscardedFrames skipped_frames() const noexcept { return skipped_frames_; }

    [[nodiscard]] DiscardedFrames oversize_frames() const noexcept { return oversize_frames_; }

private:
    explicit DbusConnection(UnixStreamSocket socket) noexcept;

    [[nodiscard]] Ex<void> start() noexcept;
    [[nodiscard]] Ex<void> flush() noexcept;
    [[nodiscard]] Ex<void> read_into_arena() noexcept;
    [[nodiscard]] Ex<std::optional<DbusIncoming>> parse_frame() noexcept;
    [[nodiscard]] Ex<void> append_message(
        const DbusHeader& header,
        std::span<const std::byte> body
    ) noexcept;
    void close() noexcept;
    [[nodiscard]] Error closed_error() const noexcept;

    std::optional<UnixStreamSocket> socket_;
    DbusAuth auth_{};
    bool ready_ = false;
    bool closed_ = false;

    WireBytes skip_remaining_{};

    struct Arenas {
        std::array<std::byte, kInboundArena> in{};
        std::array<std::byte, kOutboundArena> out{};
    };

    std::unique_ptr<Arenas> arenas_ = std::make_unique<Arenas>();
    BufferOffset in_begin_{};
    BufferOffset in_end_{};

    BufferOffset out_begin_{};
    BufferOffset out_end_{};
    BufferOffset out_limit_{};
    BufferOffset begin_offset_{};

    DbusSerial next_serial_{DbusSerial::first_user()};
    DiscardedFrames skipped_frames_{};
    DiscardedFrames oversize_frames_{};
};

static_assert(
    sizeof(DbusConnection) < 1024,
    "DbusConnection is moved by value; its arenas stay on the heap"
);

}
