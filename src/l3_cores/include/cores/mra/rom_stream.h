// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "cores/mra/rom_plan.h"
#include "infra/error.h"
#include "infra/indexed_span.h"
#include "infra/seat.h"
#include "svc/vfs.h"

namespace mister::cores::mra {

class RomStream {
    TASTY_SEAT_RESIDENT(Main);

public:
    RomStream(const svc::Vfs& vfs, RomPlan plan);

    [[nodiscard]] Ex<void> read(RomOffset from, std::span<std::uint8_t> out);

    [[nodiscard]] RomOffset end() const noexcept { return end_; }

private:
    struct Cursor {
        RomLength at{};
        RomOffset pos{};
        RomLength held{};
        bool loaded = false;
        bool started = false;
        std::unique_ptr<svc::IFile> file;
        std::vector<std::uint8_t> chunk;
    };

    struct Live {
        RomPlan::Item item;
        Cursor cur;
    };

    [[nodiscard]] Ex<void> lay_(
        const RomPlan::Segment& s,
        Cursor& c,
        RomOffset from,
        std::span<std::uint8_t> piece
    );
    [[nodiscard]] Ex<std::span<const std::uint8_t>> source_(
        const RomPlan::Segment& s,
        Cursor& c,
        RomLength at
    );
    static void lay_(const FillRun& r, RomOffset from, std::span<std::uint8_t> out) noexcept;
    static void lay_(const RomPlan::Patch& p, RomOffset from, std::span<std::uint8_t> out) noexcept;

    const svc::Vfs& vfs_;
    std::vector<Live> items_;
    RomOffset end_{};
    RomOffset next_{};
};

}
