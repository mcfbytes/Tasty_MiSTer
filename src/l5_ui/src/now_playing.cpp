// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/now_playing.h"

#include <string>

#include "app/session_identity.h"
#include "proto/conf_str.h"
#include "infra/utf8.h"
#include "ui/game_slot.h"
#include "ui/game_title.h"
#include "ui/options_model.h"

namespace mister::ui {

bool NowPlaying::on_done(
    infra::OptRef<const app::UiRequestRing::Pending> ask,
    std::span<const cores::FileSlot> slots,
    std::int64_t unix_time
) {
    TASTY_SEAT_BODY(NowPlaying);
    if (!ask) {
        ++unmatched_;
        return false;
    }
    const app::UiRequestRing::Pending& p = *ask;
    if (!p.slot)
        return false;
    const app::RememberedSlot slot = *p.slot;
    File* at = nullptr;
    for (File& f : files_)
        if (f.used && f.slot == slot && f.index == p.index)
            at = &f;
    if (p.path.empty()) {
        if (at != nullptr)
            at->used = false;
    } else {
        if (at == nullptr) {
            at = &files_[next_file_];
            next_file_ = (next_file_ + 1) % kFiles;
        }
        *at = File{.slot = slot, .index = p.index, .used = true, .path = p.path};
    }

    const bool save = slot == app::RememberedSlot::Mount
        && cores::role_of(slots, p.index) == cores::SlotRole::Save;
    if (!p.game || save)
        return false;
    game_ = true;
    const std::string title = game_title(p.path.view());
    (void)title_.assign(infra::utf8_prefix(title, Title::kCapacity));
    last_ = Played{
        .core_image = core_image_,
        .slot = slot,
        .ioctl_index = p.index,
        .path = p.path,
        .title = title_,
        .unix_time = unix_time
    };
    return true;
}

void NowPlaying::on_core_loaded(infra::OptRef<const app::UiRequestRing::Pending> load_core) {
    TASTY_SEAT_BODY(NowPlaying);
    files_ = {};
    next_file_ = 0;
    title_.clear();
    game_ = false;
    core_image_ = load_core ? load_core->path : app::PathText{};
    primary_ = PrimaryRead{};
}

std::string_view NowPlaying::file_of_(
    app::RememberedSlot slot,
    proto::IoIndex index
) const noexcept {
    for (const File& f : files_)
        if (f.used && f.slot == slot && f.index == index)
            return f.path.view();
    return {};
}

std::string_view NowPlaying::idle_line(const proto::ItemTable& items, const BuildEnv& env) {
    TASTY_SEAT_BODY(NowPlaying);
    if (!primary_.read || primary_.gen != env.conf_gen || primary_.mask != env.hdmask) {
        bool has = false;
        OptionsModel root;
        root.build(items, env);
        const GameSlot slot(root, env.slots);
        has = slot.row().has_value();
        primary_ = PrimaryRead{.gen = env.conf_gen, .mask = env.hdmask, .read = true, .has = has};
    }
    return primary_.has ? kNoGame : std::string_view{};
}

}
