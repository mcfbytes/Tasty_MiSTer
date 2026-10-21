// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/core_cell_mirror.h"

#include <iterator>
#include <string_view>

#include "app/identity_latch.h"
#include "proto/status_word.h"

namespace mister::ui {

namespace {

template <class Cell, class Reader>
bool unread(const Cell& c, const Reader& r) noexcept {
    const std::uint32_t g = c.generation();
    return g != 0 && g != r.seen();
}
}

bool CoreCellMirror::pending() const noexcept {
    const bool cheats = unread(tx_digest_cell_, tx_digest_reader_)
        || unread(cheat_catalog_cell_, cheat_catalog_reader_);
    return unread(status_cell_, status_reader_)
        || unread(dip_cell_, dip_reader_)
        || unread(option_cell_, option_reader_)
        || unread(config_slot_cell_, config_slot_reader_)
        || cheats;
}

void CoreCellMirror::forget() noexcept {
    conf_reader_.forget();
    status_reader_.forget();
    dip_reader_.forget();
    option_reader_.forget();
    config_slot_reader_.forget();
    tx_digest_reader_.forget();
    cheat_catalog_reader_.forget();
}

CoreCellMirror::Changed CoreCellMirror::sync() {
    Changed out{};
    if (conf_reader_.take_if_changed(conf_cell_, conf_scratch_)) {
        ++conf_reparses_;
        conf_gen_ = conf_scratch_.gen;
        if (conf_scratch_.truncated) {
            ++conf_truncations_;
            out.table = proto::ItemTable{};
        } else {
            const std::string_view raw{
                conf_scratch_.text,
                static_cast<std::size_t>(
                    conf_scratch_.len <= sizeof(conf_scratch_.text) ? conf_scratch_.len
                                                                    : sizeof(conf_scratch_.text)
                )
            };
            auto parsed = proto::ItemTable::parse(raw);
            if (parsed) {
                out.table = static_cast<proto::ItemTable&&>(*parsed);
            } else {
                ++conf_failures_;
                out.table = proto::ItemTable{};
            }
        }
    }
    if (status_reader_.take_if_changed(status_cell_, status_scratch_)) {
        ++status_reads_;
        static_assert(
            std::size(StatusView{}.w) == std::size(proto::StatusWord{}.words),
            "the L5 view and the L1 cell payload are the same "
            "eight words; a size split would drop the high bits "
            "item exists for"
        );
        StatusView v;
        for (std::size_t i = 0; i < std::size(v.w); ++i) {
            v.w[i] = status_scratch_.words[i];
        }
        if (set_status_words(v))
            out.repaint = true;
    }
    if (dip_reader_.take_if_changed(dip_cell_, dip_scratch_)) {
        out.repaint = true;
    }
    if (option_reader_.take_if_changed(option_cell_, option_scratch_)) {
        out.repaint = true;
    }
    if (config_slot_reader_.take_if_changed(config_slot_cell_, config_slot_scratch_)) {
        out.repaint = true;
    }
    if (sync_cheats_())
        out.repaint = true;
    return out;
}

bool CoreCellMirror::sync_cheats_() {
    bool repaint = false;
    if (cheat_catalog_reader_.take_if_changed(cheat_catalog_cell_, cheat_catalog_scratch_)) {
        if (cheat_catalog_scratch_.count != 0) {
            cheats_.take(cheat_catalog_scratch_);
            ++cheat_opens_;
        } else {
            cheats_.close();
        }
        repaint = true;
    }
    if (tx_digest_reader_.take_if_changed(tx_digest_cell_, tx_digest_scratch_)) {
        app::SessionIdentity id{};
        const bool have = identity_.copy(id);
        const cores::CheatLookup look = have && id.cheats ? *id.cheats : cores::CheatLookup{};
        if (cheats_.open(
                tx_digest_scratch_,
                have ? std::string_view{id.core} : std::string_view{},
                look
            )) {
            ++cheat_opens_;
        } else {
            ++cheat_open_failures_;
        }
        repaint = true;
    }
    return repaint;
}

bool CoreCellMirror::set_status_words(StatusView s) noexcept {
    bool changed = false;
    for (std::size_t i = 0; i < std::size(status_words_.w); ++i) {
        if (status_words_.w[i] != s.w[i]) {
            status_words_.w[i] = s.w[i];
            changed = true;
        }
    }
    return changed;
}

}
