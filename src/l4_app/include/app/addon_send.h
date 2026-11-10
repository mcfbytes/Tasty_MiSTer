// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/link_tx_channel.h"
#include "app/path_text.h"
#include "app/rung.h"
#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

struct EntryNo {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<EntryNo> of(std::size_t i) noexcept {
        if (i >= std::numeric_limits<std::uint8_t>::max())
            return std::nullopt;
        return EntryNo{static_cast<std::uint8_t>(i + 1u)};
    }

    friend constexpr bool operator==(EntryNo, EntryNo) = default;
};

struct AddonDest {
    std::uint16_t v = 0;
    static constexpr unsigned kFioBits = 8;

    [[nodiscard]] static constexpr AddonDest of(EntryNo entry, std::uint8_t fio) noexcept {
        return AddonDest{
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(entry.v) << kFioBits) | fio)
        };
    }

    [[nodiscard]] constexpr proto::WideIoIndex wide() const noexcept {
        return proto::WideIoIndex{v};
    }

    friend constexpr bool operator==(AddonDest, AddonDest) = default;
};

static_assert(
    AddonDest::of(EntryNo{3}, 0x81) == AddonDest{0x0381},
    "stock's framing: the 1-based entry number high, the FIO index low"
);
static_assert(
    !EntryNo::of(255).has_value() && EntryNo::of(254) == EntryNo{255},
    "the numbering runs 1..255, so a list stops at 255 entries"
);

class AddonSend final : public Rung {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Waiting, Done };

    struct Entry {
        std::string ext;
        AddonDest dest{};
        PathText path{};
    };

    [[nodiscard]] static AddonSend plan(
        std::string_view list,
        std::string_view pick,
        proto::WideIoIndex idx
    );

    [[nodiscard]] static std::optional<PathText> path_of(
        std::string_view pick,
        std::string_view ext
    );

    [[nodiscard]] bool take_image(std::vector<std::uint8_t> bytes);

    [[nodiscard]] Pass step(cores::ILadderHost& host, const svc::Vfs& vfs);
    [[nodiscard]] bool wants_pass(const LinkTxChannel<SeatTag::Main>& inbox) const noexcept;

    [[nodiscard]] bool wants_pass(
        const LinkTxChannel<SeatTag::Main>& inbox,
        const FileTxLevelCell& level
    ) const noexcept override;
    [[nodiscard]] Rung::Pass step(const RungEdges& edges, cores::ILadderHost& owner) override;

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    [[nodiscard]] std::uint32_t sent() const noexcept { return sent_; }

    [[nodiscard]] std::uint32_t missing() const noexcept { return missing_; }

    [[nodiscard]] std::uint32_t failed() const noexcept { return failed_; }

    [[nodiscard]] bool image_sent() const noexcept { return image_sent_; }

    [[nodiscard]] bool image_failed() const noexcept { return image_failed_; }

private:
    [[nodiscard]] bool open_(const svc::Vfs& vfs);

    [[nodiscard]] bool next_is_image() const noexcept;

    std::vector<Entry> entries_;
    std::vector<std::uint8_t> image_;
    std::optional<EntryNo> image_at_{};
    std::size_t next_ = 0;
    std::optional<cores::PayloadPieces> pieces_{};
    std::uint32_t sent_ = 0;
    std::uint32_t missing_ = 0;
    std::uint32_t failed_ = 0;
    bool image_sent_ = false;
    bool image_failed_ = false;
};

}
