// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/fio_cheat_sink.h"

#include "cores/cheat_geometry.h"
#include "proto/download_session.h"

namespace mister::cores {

static_assert(kCheatTableBytes % CheatGeometry{}.unit == 0,
              "req: the default unit divides the table exactly");
static_assert(clamp_cheat_geometry(16, 128).max_active == 128,
              "req: the default pair sits ON the cap, not under it");

Ex<void> FioCheatSink::apply(std::span<const std::uint8_t> table, std::uint32_t unit) {
    TASTY_SEAT_BODY(FioCheatSink);

    (void)unit;
    if (queue_ != nullptr) (void)queue_->flush();
    if (auto r = proto::DownloadSession::send_cheats(*link_, table); !r) return r;
    ++sends_;
    bytes_ += table.size();
    return {};
}

}  // namespace mister::cores
