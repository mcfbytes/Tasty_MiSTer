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
        || unread(dip_labels_cell_, dip_labels_reader_)
        || unread(dip_choices_cell_, dip_choices_reader_)
        || unread(option_cell_, option_reader_)
        || unread(config_slot_cell_, config_slot_reader_)
        || cheats;
}

void CoreCellMirror::forget() noexcept {
    conf_reader_.forget();
    status_reader_.forget();
    dip_labels_reader_.forget();
    dip_choices_reader_.forget();
    option_reader_.forget();
    config_slot_reader_.forget();
    tx_digest_reader_.forget();
    cheat_catalog_reader_.forget();
}

bool CoreCellMirror::read_conf_(Changed& out) {
    if (!conf_reader_.take_if_changed(conf_cell_, conf_scratch_))
        return false;
    ++conf_reparses_;
    conf_gen_ = conf_scratch_.gen;
    if (conf_scratch_.truncated) {
        ++conf_truncations_;
        out.table = proto::ItemTable{};
    } else {
        auto parsed = proto::ItemTable::parse(conf_scratch_.view());
        if (parsed) {
            out.table = static_cast<proto::ItemTable&&>(*parsed);
        } else {
            ++conf_failures_;
            out.table = proto::ItemTable{};
        }
    }
    return true;
}

CoreCellMirror::Changed CoreCellMirror::sync() {
    Changed out{};
    (void)read_conf_(out);
    if (status_reader_.take_if_changed(status_cell_, status_scratch_)) {
        ++status_reads_;
        StatusView v;
        v.status = status_scratch_;
        if (set_status_words(v))
            out.repaint = true;
    }
    if (sync_dips_())
        out.repaint = true;
    if (option_reader_.take_if_changed(option_cell_, option_scratch_)) {
        out.repaint = true;
    }
    if (config_slot_reader_.take_if_changed(config_slot_cell_, config_slot_scratch_)) {
        out.repaint = true;
    }
    if (sync_cheats_(out))
        out.repaint = true;
    return out;
}

bool CoreCellMirror::sync_dips_() {
    if (dip_choices_reader_.take_if_changed(dip_choices_cell_, dip_choices_scratch_)) {
        dip_choices_pending_ = true;
    }
    if (dip_labels_reader_.take_if_changed(dip_labels_cell_, dip_table_.labels)) {
        if (dip_choices_pending_ && dip_choices_scratch_.gen == dip_table_.labels.gen) {
            dip_table_.choices = dip_choices_scratch_;
            dip_choices_pending_ = false;
        } else {
            dip_table_.choices = app::DipChoices{};
        }
        return true;
    }
    if (dip_choices_pending_ && dip_choices_scratch_.gen == dip_table_.labels.gen) {
        dip_table_.choices = dip_choices_scratch_;
        dip_choices_pending_ = false;
        return true;
    }
    return false;
}

bool CoreCellMirror::sync_cheats_(Changed& out) {
    bool repaint = false;
    const bool catalog_new = cheat_catalog_reader_.take_if_changed(
        cheat_catalog_cell_,
        cheat_catalog_scratch_
    );
    if (catalog_new) {
        if (cheat_catalog_scratch_.count != 0) {
            cheats_.take(cheat_catalog_scratch_);
            ++cheat_opens_;
        } else {
            cheats_.close();
        }
        repaint = true;
    }

    if (const auto probe = tx_digest_cell_.sample(); probe
        && probe.generation != tx_digest_reader_.seen()
        && probe.value.gen.ahead_of(conf_gen_)) {
        (void)read_conf_(out);
        if (probe.value.gen.ahead_of(conf_gen_))
            return repaint;
    }

    if (interleave_)
        interleave_->before_digest_read();
    if (tx_digest_reader_.take_if_changed(tx_digest_cell_, tx_digest_scratch_)) {
        if (tx_digest_scratch_.gen != conf_gen_)
            (void)read_conf_(out);
        if (tx_digest_scratch_.gen == conf_gen_) {
            const std::optional<app::SessionIdentity> id = identity_.copy();
            const cores::CheatLookup look = id && id->cheats ? *id->cheats : cores::CheatLookup{};
            if (cheats_.open(tx_digest_scratch_, id ? id->core.view() : std::string_view{}, look)) {
                ++cheat_opens_;
            } else {
                ++cheat_open_failures_;
            }
        } else if (!catalog_new) {

            cheats_.close();
        }
        repaint = true;
    }
    return repaint;
}

bool CoreCellMirror::set_status_words(StatusView s) noexcept {
    if (status_words_.status == s.status)
        return false;
    status_words_ = s;
    return true;
}

}
