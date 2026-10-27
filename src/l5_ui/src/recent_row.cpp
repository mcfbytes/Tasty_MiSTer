// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/recent_row.h"

#include "ui/game_title.h"

namespace mister::ui {

RecentRow::RecentRow(RecentLaunch& launch, app::RecentGames::Record record, app::RowTier tier)
    : launch_(launch),
      record_(static_cast<app::RecentGames::Record&&>(record)),
      tier_(tier) {
    TASTY_SEAT_BODY(RecentRow);

    title_ = record_.title.empty() ? game_title(record_.path) : record_.title;
    ask_text_[0] = "Switch to ";
    ask_text_[0].append(record_.core);
    ask_text_[1] = "and load ";
    ask_text_[1].append(title_);
    ask_text_[1].push_back('?');
    ask_body_[0] = ask_text_[0];
    ask_body_[1] = ask_text_[1];
}

bool RecentRow::shown() const {
    TASTY_SEAT_BODY(RecentRow);
    return !title_.empty();
}

std::optional<RowKey> RecentRow::key() const {
    TASTY_SEAT_BODY(RecentRow);

    std::uint32_t h = 2166136261u;
    for (const char c : record_.core)
        h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
    for (const char c : record_.path)
        h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
    return RowKey{kKeySpace | (h & ~kKeySpaceMask)};
}

app::PageDescription::Row RecentRow::describe() const {
    TASTY_SEAT_BODY(RecentRow);
    app::PageDescription::Row out{};
    out.kind = app::PageDescription::RowKind::Action;
    out.tier = tier_;
    (void)out.label.assign(title_);
    (void)out.value.assign(record_.core);
    return out;
}

Transition RecentRow::select() {
    TASTY_SEAT_BODY(RecentRow);
    if (!launch_.asks_first(record_))
        return launch_.launch(record_);
    if (!launch_.ask(*this, title_, ask_body_))
        return Transition{};
    return Transition{Transition::Kind::Push, PageId::Confirm};
}

Transition RecentRow::on_confirm(std::uint8_t choice) {
    TASTY_SEAT_BODY(RecentRow);
    if (choice != 0)
        return Transition{Transition::Kind::Pop};
    return launch_.launch(record_);
}

}
