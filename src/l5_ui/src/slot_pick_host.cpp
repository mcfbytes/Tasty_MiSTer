// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/slot_pick_host.h"

#include <string>
#include <utility>

#include "app/event.h"
#include "app/info_sink.h"
#include "app/name_config.h"
#include "app/romset_alias_reader.h"
#include "infra/ascii.h"
#include "ui/browser_entry.h"
#include "ui/browser_launch.h"
#include "ui/build_env.h"
#include "ui/game_title.h"
#include "ui/option_item.h"
#include "ui/page_ask.h"

namespace mister::ui {

SlotPickHost::Env::Env(const BuildEnv& env)
    : core_dir(env.core_dir),
      cue_dir(env.cue_dir),
      file_stem(env.file_stem),
      slots(env.slots),
      image_no_zip(env.image_no_zip),
      conf_gen(env.conf_gen) {}

SlotPickHost::SlotPickHost(const Env& env, Borrows b)
    : env_(env),
      b_(b),
      romsets_(b.files ? std::make_unique<app::RomsetAliasReader>(*b.files) : nullptr) {}

SlotPickHost::~SlotPickHost() = default;

void SlotPickHost::ask_mount(proto::IoIndex slot, std::string_view path) {
    const app::CoreScope scope{env_.conf_gen};
    const bool landed = path.empty()
        ? push_ask(b_.asks, app::UiRequest::UnmountImage{.index = slot, .scope = scope})
        : push_ask(b_.asks, app::UiRequest::MountImage{.index = slot, .scope = scope}, path);
    if (!landed) {
        refusals_.add();
        return;
    }
    ++published_;
}

BrowserSession::Request SlotPickHost::browser_request(
    const OptionItem& it,
    proto::FileRowId row,
    bool offer_recent
) {
    arm_(it, row);
    BrowserSession::Request r;
    r.extensions = it.ext_list;
    r.scan.include_dirs = true;

    const std::string_view home = browse_home_(it);
    r.games_dir = home;
    r.recents_core = home;
    r = it.shape_browse(
        std::move(r),
        OptionItem::BrowseFacts{
            .slots = env_.slots,
            .image_no_zip = env_.image_no_zip,
            .romsets = infra::opt_ref_of(romsets_.get())
        }
    );
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(this));
    r.recents = app::RecentsStore::ListId::of(row);

    if (offer_recent && b_.recents && !b_.recents->load(r.recents_core, *r.recents).empty()) {
        r.recent_entry = true;
    }
    return r;
}

std::string_view SlotPickHost::browse_home_(const OptionItem& it) const noexcept {
    const std::string_view el = it.ext_list;
    const bool cue_row = !env_.cue_dir.empty() && infra::istarts_with(el, "CUE");
    return cue_row ? std::string_view{env_.cue_dir} : std::string_view{env_.core_dir};
}

void SlotPickHost::arm_(const OptionItem& it, proto::FileRowId row) noexcept {
    pending_row_ = row;
    pending_item_ = it.item;
    pending_gen_ = env_.conf_gen;
    pending_store_ = it.store_name;
}

std::optional<SlotPickHost::RecentsRef> SlotPickHost::recents_ref(const OptionItem& it) const {
    const std::optional<proto::FileRowId> row = it.file_row();
    if (!row)
        return std::nullopt;
    return RecentsRef{
        .id = app::RecentsStore::ListId::of(*row),
        .core = std::string{browse_home_(it)}
    };
}

bool SlotPickHost::has_recents(const OptionItem& it) const {
    if (!b_.recents)
        return false;
    const std::optional<RecentsRef> ref = recents_ref(it);
    return ref && !b_.recents->load(ref->core, ref->id).empty();
}

Transition SlotPickHost::open(const OptionItem& it, bool offer_recent) {
    const std::optional<proto::FileRowId> row = it.file_row();
    if (!row || !b_.browser) {
        refusals_.add();
        return Transition{};
    }
    b_.browser->pending = browser_request(it, *row, offer_recent);
    return Transition::push(PageId::Browser);
}

Transition SlotPickHost::open_recents(const OptionItem& it) {
    const std::optional<proto::FileRowId> row = it.file_row();
    if (!row || !b_.browser || !b_.recents) {
        refusals_.add();
        return Transition{};
    }
    BrowserSession::Request r = browser_request(it, *row, false);
    if (b_.recents->load(r.recents_core, *r.recents).empty())
        return Transition{};
    r.recents_first = true;
    b_.browser->pending = static_cast<BrowserSession::Request&&>(r);
    return Transition::push(PageId::Browser);
}

Transition SlotPickHost::pick(const OptionItem& it, std::string_view path) {
    TASTY_SEAT_BODY(SlotPickHost);
    const std::optional<proto::FileRowId> row = it.file_row();
    if (path.empty() || !row) {
        refusals_.add();
        return Transition{};
    }
    arm_(it, *row);
    BrowserEntry entry;
    entry.altname = game_title(path);
    return on_picked(entry, path);
}

Transition SlotPickHost::eject(const OptionItem& it) {
    TASTY_SEAT_BODY(SlotPickHost);
    const std::optional<proto::FileRowId> row = it.file_row();
    if (!row || row->kind != proto::FileRowKind::Mount || !b_.asks) {
        refusals_.add();
        return Transition{};
    }
    arm_(it, *row);
    unmount_(*row);
    return Transition::pop();
}

void SlotPickHost::unmount_(proto::FileRowId row) {
    if (pending_store_
        && b_.names
        && !b_.names->save_path(env_.file_stem, proto::FileRowId::mount(row.index), {})) {
        refusals_.add();
    }
    ask_mount(row.index, {});
}

Transition SlotPickHost::on_picked(const BrowserEntry& e, std::string_view path) {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!b_.asks || path.empty() || !pending_row_) {
        refusals_.add();
        return Transition::close_all();
    }

    if (b_.recents) {
        const app::RecentsStore::ListId id = app::RecentsStore::ListId::of(*pending_row_);
        if (!b_.recents->update(env_.core_dir, id, path, e.altname))
            refusals_.add();
    }
    if (pending_store_ && b_.names) {
        if (!b_.names->save_path(env_.file_stem, *pending_row_, path))
            refusals_.add();
    }

    switch (pending_row_->kind) {
        case proto::FileRowKind::Mount:
            ask_mount(pending_row_->index, path);
            return Transition::close_all();
        case proto::FileRowKind::File: {

            const app::UiRequest::LoadFile ask{
                .index = pending_row_->index,
                .item = pending_item_,
                .scope = {pending_gen_}
            };
            if (!push_ask(b_.asks, ask, path)) {
                refusals_.add();
                return Transition::close_all();
            }
            ++published_;
            return Transition::close_all();
        }
    }
    return Transition::close_all();
}

Transition SlotPickHost::on_cancel() {
    TASTY_SEAT_BODY(SlotPickHost);
    return Transition::pop();
}

Transition SlotPickHost::on_eject() {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!pending_row_ || pending_row_->kind != proto::FileRowKind::Mount) {
        refusals_.add();
        return Transition{};
    }
    unmount_(*pending_row_);
    return Transition::close_all();
}

Transition SlotPickHost::on_clear() {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!pending_store_ || !b_.names || !pending_row_) {
        refusals_.add();
        return Transition{};
    }
    if (!b_.names->save_path(env_.file_stem, proto::FileRowId::file(pending_row_->index), {})) {
        refusals_.add();
    }
    if (b_.info)
        b_.info->info(app::InfoId::OptionCleared);
    return Transition::close_all();
}

}
