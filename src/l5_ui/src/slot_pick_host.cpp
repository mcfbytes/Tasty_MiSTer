// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/slot_pick_host.h"

#include <span>
#include <string>

#include "app/event.h"
#include "app/info_sink.h"
#include "app/name_config.h"
#include "app/romset_alias_reader.h"
#include "infra/ascii.h"
#include "app/session_identity.h"
#include "ui/browser_entry.h"
#include "ui/browser_launch.h"
#include "ui/build_env.h"
#include "ui/game_title.h"
#include "ui/option_item.h"
#include "ui/page_ask.h"

namespace mister::ui {
namespace {

std::string with_chd(std::string ext) {
    constexpr std::size_t kFieldWidth = 3;
    while (ext.size() % kFieldWidth != 0)
        ext.push_back(' ');
    constexpr std::string_view kChd = "CHD";
    if (!infra::icontains(ext, kChd))
        ext.append(kChd);
    return ext;
}

SlotFocus slot_of(const OptionItem& it) noexcept {
    return SlotFocus{
        it.mounts_image() ? SlotFocus::Kind::Image : SlotFocus::Kind::File,
        it.ioctl_index
    };
}

app::RecentsStore::ListId recents_list_of(SlotFocus slot) noexcept {
    const app::RecentsStore::ListId::Kind kind = slot.kind == SlotFocus::Kind::Image
        ? app::RecentsStore::ListId::Kind::Mount
        : app::RecentsStore::ListId::Kind::File;
    return app::RecentsStore::ListId{kind, slot.index};
}

app::NameConfig::Slot remembered_slot_of(SlotFocus slot) noexcept {
    return slot.kind == SlotFocus::Kind::Image ? app::NameConfig::Slot::Mount
                                               : app::NameConfig::Slot::File;
}

}

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
        ++refusals_;
        return;
    }
    ++published_;
}

BrowserSession::Request SlotPickHost::browser_request(const OptionItem& it, bool offer_recent) {
    arm_(it);
    BrowserSession::Request r;
    r.extensions = it.ext_list;
    r.scan.include_dirs = true;

    const std::string_view home = browse_home_(it);
    r.games_dir = home;
    r.recents_core = home;

    const std::span<const cores::FileSlot> slots = env_.slots;
    if (it.opens_browser() && pending_slot_.kind == SlotFocus::Kind::File) {
        for (const cores::FileSlot& s : slots) {
            if (s.index != pending_slot_.index)
                continue;
            r.scan.no_enter = s.no_enter;
            if (s.romset_browse.has_value()) {
                r.scan.archive_browse = *s.romset_browse;
                if (romsets_) {
                    romsets_->bind(s);
                    r.aliases = infra::opt_ref_of<app::IRomsetAliases>(romsets_.get());
                }
            }
            break;
        }
    }
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(this));
    switch (pending_slot_.kind) {
        case SlotFocus::Kind::Image: {

            const cores::SlotRole role = cores::role_of(slots, pending_slot_.index);
            r.policy.saves_home = role == cores::SlotRole::Save;
            if (role == cores::SlotRole::Disc)
                r.extensions = with_chd(std::move(r.extensions));
            bool opens_zip = false;
            for (const cores::FileSlot& s : slots)
                if (s.index == pending_slot_.index)
                    opens_zip = s.opens_zip;
            r.scan.no_zip = env_.image_no_zip && !opens_zip;
            r.policy.eject_on_backspace = true;
            break;
        }
        case SlotFocus::Kind::File:
            r.policy.clear_on_backspace = pending_store_;
            break;
    }
    r.recents = recents_list_of(pending_slot_);

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

void SlotPickHost::arm_(const OptionItem& it) noexcept {
    pending_slot_ = slot_of(it);
    pending_item_ = it.item;
    pending_gen_ = env_.conf_gen;
    pending_store_ = it.store_name;
}

SlotPickHost::RecentsRef SlotPickHost::recents_ref(const OptionItem& it) const {
    return RecentsRef{.id = recents_list_of(slot_of(it)), .core = std::string{browse_home_(it)}};
}

bool SlotPickHost::has_recents(const OptionItem& it) const {
    if (!b_.recents)
        return false;
    const RecentsRef ref = recents_ref(it);
    return !b_.recents->load(ref.core, ref.id).empty();
}

app::RecentsStore::ListId SlotPickHost::recents_id_for_pending() const noexcept {
    return recents_list_of(pending_slot_);
}

Transition SlotPickHost::open(const OptionItem& it, bool offer_recent) {
    if (!b_.browser) {
        ++refusals_;
        return Transition{};
    }
    b_.browser->pending = browser_request(it, offer_recent);
    return Transition{Transition::Kind::Push, PageId::Browser};
}

Transition SlotPickHost::open_recents(const OptionItem& it) {
    if (!b_.browser || !b_.recents) {
        ++refusals_;
        return Transition{};
    }
    BrowserSession::Request r = browser_request(it, false);
    if (b_.recents->load(r.recents_core, *r.recents).empty())
        return Transition{};
    r.recents_first = true;
    b_.browser->pending = static_cast<BrowserSession::Request&&>(r);
    return Transition{Transition::Kind::Push, PageId::Browser};
}

Transition SlotPickHost::pick(const OptionItem& it, std::string_view path) {
    TASTY_SEAT_BODY(SlotPickHost);
    if (path.empty()) {
        ++refusals_;
        return Transition{};
    }
    arm_(it);
    BrowserEntry entry;
    entry.altname = game_title(path);
    return on_picked(entry, path);
}

Transition SlotPickHost::eject(const OptionItem& it) {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!b_.asks || slot_of(it).kind != SlotFocus::Kind::Image) {
        ++refusals_;
        return Transition{};
    }
    arm_(it);
    unmount_pending_();
    return Transition{Transition::Kind::Pop};
}

void SlotPickHost::unmount_pending_() {
    if (pending_store_
        && b_.names
        && !b_.names->save_path(
            env_.file_stem,
            app::NameConfig::Slot::Mount,
            pending_slot_.index,
            {}
        )) {
        ++refusals_;
    }
    ask_mount(pending_slot_.index, {});
}

Transition SlotPickHost::on_picked(const BrowserEntry& e, std::string_view path) {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!b_.asks || path.empty()) {
        ++refusals_;
        return Transition{Transition::Kind::CloseAll};
    }

    if (b_.recents
        && !b_.recents->update(env_.core_dir, recents_id_for_pending(), path, e.altname)) {
        ++refusals_;
    }
    if (pending_store_ && b_.names) {
        if (!b_.names->save_path(
                env_.file_stem,
                remembered_slot_of(pending_slot_),
                pending_slot_.index,
                path
            ))
            ++refusals_;
    }

    switch (pending_slot_.kind) {
        case SlotFocus::Kind::Image:
            ask_mount(pending_slot_.index, path);
            return Transition{Transition::Kind::CloseAll};
        case SlotFocus::Kind::File: {

            const app::UiRequest::LoadFile ask{
                .index = pending_slot_.index,
                .item = pending_item_,
                .scope = {pending_gen_}
            };
            if (!push_ask(b_.asks, ask, path)) {
                ++refusals_;
                return Transition{Transition::Kind::CloseAll};
            }
            ++published_;
            return Transition{Transition::Kind::CloseAll};
        }
    }
    return Transition{Transition::Kind::CloseAll};
}

Transition SlotPickHost::on_cancel() {
    TASTY_SEAT_BODY(SlotPickHost);
    return Transition{Transition::Kind::Pop};
}

Transition SlotPickHost::on_eject() {
    TASTY_SEAT_BODY(SlotPickHost);
    if (pending_slot_.kind != SlotFocus::Kind::Image) {
        ++refusals_;
        return Transition{};
    }
    unmount_pending_();
    return Transition{Transition::Kind::CloseAll};
}

Transition SlotPickHost::on_clear() {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!pending_store_ || !b_.names) {
        ++refusals_;
        return Transition{};
    }
    if (!b_.names
             ->save_path(env_.file_stem, app::NameConfig::Slot::File, pending_slot_.index, {})) {
        ++refusals_;
    }
    if (b_.info)
        b_.info->info(app::InfoId::OptionCleared);
    return Transition{Transition::Kind::CloseAll};
}

}
