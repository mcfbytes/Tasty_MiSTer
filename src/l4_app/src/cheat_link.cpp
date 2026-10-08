// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cheat_link.h"

#include <cstddef>
#include <cstring>
#include <span>

#include "cores/cheat_records.h"
#include "cores/cheat_sink.h"
#include "cores/core_support.h"

namespace mister::app {

namespace {

template <std::size_t N>
void copy_cell_text(char (&dst)[N], std::string_view src) noexcept {
    const std::size_t n = src.size() < N - 1 ? src.size() : N - 1;
    if (n != 0) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}
}  // namespace

void CheatLink::session_up(cores::Core* core) noexcept {
    TASTY_SEAT_BODY(CheatLink);
    catalog_scratch_ = CheatCatalog{};
    cores::ICheatRecords* rec = core != nullptr ? core->cheat_records() : nullptr;
    if (rec != nullptr) {
        const cores::CheatGeometry g = rec->cheat_geometry();
        catalog_scratch_.unit = g.unit;
        catalog_scratch_.max_active = g.max_active;
        const std::size_t have = rec->cheat_count();
        for (std::size_t i = 0; i < have; ++i) {
            const auto bytes = rec->cheat_bytes(i);
            const std::size_t used = catalog_scratch_.used;
            if (catalog_scratch_.count >= CheatCatalog::kMaxRows ||
                bytes.size() > CheatCatalog::kArenaBytes - used) {
                ++catalog_scratch_.dropped;
                continue;
            }
            CheatCatalog::Row& row = catalog_scratch_.rows[catalog_scratch_.count];
            copy_cell_text(row.name, rec->cheat_name(i));
            row.offset = static_cast<std::uint16_t>(used);
            row.len = static_cast<std::uint16_t>(bytes.size());
            std::memcpy(catalog_scratch_.data + used, bytes.data(), bytes.size());
            catalog_scratch_.used = static_cast<std::uint16_t>(used + bytes.size());
            ++catalog_scratch_.count;
        }
    }
    catalog_cell_.publish(catalog_scratch_);
}

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
