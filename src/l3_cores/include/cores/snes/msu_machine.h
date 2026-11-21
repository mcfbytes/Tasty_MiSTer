// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/mailbox_servant.h"
#include "cores/snes/msu_wire.h"
#include "cores/snes/sector_read_ahead.h"
#include "hal/spi_halves.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores::snes {

class MsuMachine final : public IMailboxServant {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    explicit MsuMachine(const svc::Vfs& vfs) noexcept : vfs_(vfs) {}

    MsuMachine(const MsuMachine&) = delete;
    MsuMachine& operator=(const MsuMachine&) = delete;

    [[nodiscard]] proto::MailboxAct serve(
        const proto::MailboxFrame& frame,
        std::span<std::uint8_t, proto::kMailboxActBytes> bytes
    ) noexcept override;
    void begin_core() noexcept override;
    void rebind(std::string_view stem, bool present) noexcept override;
    void refill_one() noexcept override;
    [[nodiscard]] bool rest() const noexcept override;
    void release() noexcept override;

    struct Counts {
        std::uint32_t resets = 0;
        std::uint32_t tracks = 0;
        std::uint32_t missing = 0;
        std::uint32_t sectors = 0;
        std::uint32_t ignored = 0;
    };

    [[nodiscard]] const Counts& counts() const noexcept { return counts_; }

    [[nodiscard]] const SectorReadAhead& ahead() const noexcept { return ahead_; }

    [[nodiscard]] msu::TrackCursor cursor() const noexcept { return cursor_; }

    [[nodiscard]] std::string_view track_path() const noexcept { return path_.view(); }

    [[nodiscard]] bool track_open() const noexcept { return file_ != nullptr; }

private:
    struct AudioTrack {
        std::uint32_t v = 0;

        [[nodiscard]] static constexpr AudioTrack of(hal::SpiHalves h) noexcept {
            return h.joined([](std::uint32_t d) { return AudioTrack{d}; });
        }

        [[nodiscard]] std::string_view name_into(std::span<char> buf) const noexcept {
            const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
            return ec == std::errc{} ? std::string_view(buf.data(), end) : std::string_view{};
        }

        friend constexpr bool operator==(AudioTrack, AudioTrack) = default;
    };

    struct SectorLba {
        std::uint32_t v = 0;

        [[nodiscard]] static constexpr SectorLba of(hal::SpiHalves h) noexcept {
            return h.joined([](std::uint32_t d) { return SectorLba{d}; });
        }

        [[nodiscard]] constexpr std::uint32_t byte_of() const noexcept {
            return msu::kSectorSize.window_bytes(proto::BlockCount{v});
        }

        friend constexpr bool operator==(SectorLba, SectorLba) = default;
    };

    [[nodiscard]] proto::MailboxAct track_(AudioTrack track) noexcept;
    [[nodiscard]] proto::MailboxAct next_(
        std::span<std::uint8_t, proto::kMailboxActBytes> bytes
    ) noexcept;
    void seek_(SectorLba sector) noexcept;
    void close_() noexcept;

    const svc::Vfs& vfs_;
    infra::PathText stem_{};
    infra::PathText path_{};
    bool present_ = false;
    std::optional<std::uint8_t> last_req_{};
    std::unique_ptr<svc::IFile> file_{};
    proto::FileSize size_{};
    msu::TrackCursor cursor_{};
    SectorReadAhead ahead_{};
    Counts counts_{};
};

}
