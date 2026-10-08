// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cheat_link.h"

#include <cstddef>
#include <span>

#include "cores/cheat_sink.h"
#include "cores/core_support.h"

namespace mister::app {

void CheatLink::content_loaded(cores::Core* core, TxDigest::Kind kind, std::string_view path,
                               std::uint32_t crc, bool same_game) noexcept {
    TASTY_SEAT_BODY(CheatLink);
    if (core != nullptr) {
        if (cores::ICheatSink* sink = core->cheat_sink(); sink != nullptr) {
            if (auto r = sink->apply({}, 16); !r) {
                ++refusals_;
            }
        }
    }
    blob_reader_.forget();
    digest_scratch_ = TxDigest{};
    digest_scratch_.kind = kind;
    digest_scratch_.crc = crc;
    digest_scratch_.same_game = same_game;
    (void)digest_scratch_.path.assign(path);
    digest_cell_.publish(digest_scratch_);
}

bool CheatLink::apply_cheats_counted(const CheatCall& call) noexcept {
    TASTY_SEAT_BODY(CheatLink);
    if (call.core == nullptr) {
        ++refusals_;
        return false;
    }

    if (call.fio_held) {
        ++refusals_;
        return false;
    }
    if (!blob_reader_.take_if_changed(blob_, blob_scratch_)) {

        ++refusals_;
        return true;
    }
    cores::ICheatSink* sink = call.core->cheat_sink();
    if (sink == nullptr) {
        ++refusals_;
        return false;
    }
    const std::size_t n = blob_scratch_.len <= sizeof(blob_scratch_.bytes)
                              ? blob_scratch_.len
                              : sizeof(blob_scratch_.bytes);
    if (auto r =
            sink->apply(std::span<const std::uint8_t>(blob_scratch_.bytes, n), blob_scratch_.unit);
        !r) {
        ++refusals_;
        return false;
    }
    ++applies_;
    return true;
}

}  // namespace mister::app
