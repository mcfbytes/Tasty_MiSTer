// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/mailbox_servant.h"
#include "cores/msu_wire.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores {

class MsuMachine final : public IMailboxServant {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr std::size_t kAhead = 8;

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
        std::uint32_t ahead_hits = 0;
        std::uint32_t ahead_reads = 0;
        std::uint32_t sync_reads = 0;
        std::uint32_t read_errors = 0;
        std::uint32_t ignored = 0;
    };

    [[nodiscard]] const Counts& counts() const noexcept { return counts_; }

    [[nodiscard]] std::uint64_t cursor() const noexcept { return cursor_; }

    [[nodiscard]] std::string_view track_path() const noexcept { return path_.view(); }

    [[nodiscard]] bool track_open() const noexcept { return file_ != nullptr; }

private:
    struct TrackNumber {
        std::uint32_t v = 0;

        [[nodiscard]] std::string_view name_into(std::span<char> buf) const noexcept {
            const auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
            return ec == std::errc{} ? std::string_view(buf.data(), end) : std::string_view{};
        }

        friend constexpr bool operator==(TrackNumber, TrackNumber) = default;
    };

    struct SectorIndex {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr std::uint32_t byte_of() const noexcept {
            return v * msu::kSectorBytes;
        }

        friend constexpr bool operator==(SectorIndex, SectorIndex) = default;
    };

    struct Sector {
        std::uint64_t off = 0;
        std::uint16_t got = 0;
        std::array<std::uint8_t, msu::kSectorBytes> bytes{};
    };

    [[nodiscard]] proto::MailboxAct track_(TrackNumber track) noexcept;
    [[nodiscard]] proto::MailboxAct next_(
        std::span<std::uint8_t, proto::kMailboxActBytes> bytes
    ) noexcept;
    void seek_(SectorIndex sector) noexcept;
    void drop_ahead_(std::uint64_t from) noexcept;

    [[nodiscard]] std::span<std::uint8_t> read_(
        std::uint64_t off,
        std::span<std::uint8_t> dst
    ) noexcept;
    void close_() noexcept;

    const svc::Vfs& vfs_;
    infra::PathText stem_{};
    infra::PathText path_{};
    bool present_ = false;
    std::uint8_t last_req_ = 255;
    std::unique_ptr<svc::IFile> file_{};
    proto::FileSize size_{};
    std::uint64_t cursor_ = 0;

    std::array<Sector, kAhead> ahead_{};
    std::size_t head_ = 0;
    std::size_t held_ = 0;
    std::uint64_t fill_at_ = 0;
    bool fill_done_ = true;
    Counts counts_{};
};

}
