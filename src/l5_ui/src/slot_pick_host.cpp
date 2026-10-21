// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/slot_pick_host.h"

#include <algorithm>
#include <span>
#include <string>

#include "app/event.h"
#include "app/info_sink.h"
#include "app/name_config.h"
#include "app/romset_alias_reader.h"
#include "app/session_identity.h"
#include "ui/browser_entry.h"
#include "ui/browser_launch.h"
#include "ui/build_env.h"
#include "ui/options_page.h"
#include "ui/page_ask.h"

namespace mister::ui {
namespace {

std::string with_chd(std::string ext) {
    while (ext.size() % 3 != 0)
        ext.push_back(' ');
    constexpr std::string_view kChd = "CHD";
    const auto upper_eq = [](char a, char b) {
        return (a >= 'a' && a <= 'z' ? static_cast<char>(a - 'a' + 'A') : a) == b;
    };
    if (std::search(ext.begin(), ext.end(), kChd.begin(), kChd.end(), upper_eq) == ext.end())
        ext.append(kChd);
    return ext;
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

void SlotPickHost::ask_mount(std::uint8_t slot, std::string_view path) {
    const proto::IoIndex index{slot};
    const app::CoreScope scope{env_.conf_gen};
    const bool landed = path.empty()
        ? push_ask(b_.asks, app::UiRequest::UnmountImage{.index = index, .scope = scope})
        : push_ask(b_.asks, app::UiRequest::MountImage{.index = index, .scope = scope}, path);
    if (!landed) {
        ++refusals_;
        return;
    }
    ++published_;
}

BrowserSession::Request SlotPickHost::browser_request(const OptionItem& it, bool offer_recent) {
    BrowserSession::Request r;
    r.extensions = it.ext_list;
    r.scan.include_dirs = true;

    const std::string_view home = browse_home_(it);
    r.games_dir = home;
    r.recents_core = home;

    const std::span<const cores::FileSlot> slots = env_.slots;
    if (it.opens_browser() && !it.mounts_image()) {
        for (const cores::FileSlot& s : slots) {
            if (s.index.v != it.ioctl_index)
                continue;
            r.scan.romset_browse = s.romset_browse;
            r.scan.no_enter = s.no_enter;
            if (s.romset_browse && romsets_) {
                romsets_->bind(s);
                r.aliases = infra::opt_ref_of<app::IRomsetAliases>(romsets_.get());
            }
            break;
        }
    }
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(this));
    if (it.mounts_image()) {

        const cores::SlotRole role = cores::role_of(slots, proto::IoIndex{it.ioctl_index});
        r.policy.saves_home = role == cores::SlotRole::Save;
        if (role == cores::SlotRole::Disc)
            r.extensions = with_chd(std::move(r.extensions));
        bool opens_zip = false;
        for (const cores::FileSlot& s : slots)
            if (s.index.v == it.ioctl_index)
                opens_zip = s.opens_zip;
        r.scan.no_zip = env_.image_no_zip && !opens_zip;
        r.policy.eject_on_backspace = true;
        r.recents = app::RecentsStore::ListId{
            app::RecentsStore::ListId::Kind::Mount,
            it.ioctl_index
        };
    } else {
        r.policy.clear_on_backspace = it.store_name;
        r.recents = app::RecentsStore::ListId{
            app::RecentsStore::ListId::Kind::File,
            it.ioctl_index
        };
    }
    arm_(it);

    if (offer_recent && b_.recents && !b_.recents->load(r.recents_core, *r.recents).empty()) {
        r.recent_entry = true;
    }
    return r;
}

std::string_view SlotPickHost::browse_home_(const OptionItem& it) const noexcept {
    const std::string_view el = it.ext_list;
    const bool cue_row = !env_.cue_dir.empty()
        && el.size() >= 3
        && (el[0] == 'C' || el[0] == 'c')
        && (el[1] == 'U' || el[1] == 'u')
        && (el[2] == 'E' || el[2] == 'e');
    return cue_row ? std::string_view{env_.cue_dir} : std::string_view{env_.core_dir};
}

void SlotPickHost::arm_(const OptionItem& it) noexcept {
    pending_mounts_ = it.mounts_image();
    pending_slot_ = it.ioctl_index;
    pending_item_ = it.item;
    pending_gen_ = env_.conf_gen;
    pending_store_ = it.store_name;
}

SlotPickHost::RecentsRef SlotPickHost::recents_ref(const OptionItem& it) const {
    return RecentsRef{
        .id =
            app::RecentsStore::ListId{
                it.mounts_image() ? app::RecentsStore::ListId::Kind::Mount
                                  : app::RecentsStore::ListId::Kind::File,
                it.ioctl_index
            },
        .core = std::string{browse_home_(it)},
    };
}

bool SlotPickHost::has_recents(const OptionItem& it) const {
    if (!b_.recents)
        return false;
    const RecentsRef ref = recents_ref(it);
    return !b_.recents->load(ref.core, ref.id).empty();
}

app::RecentsStore::ListId SlotPickHost::recents_id_for_pending() const noexcept {
    return app::RecentsStore::ListId{
        pending_mounts_ ? app::RecentsStore::ListId::Kind::Mount
                        : app::RecentsStore::ListId::Kind::File,
        pending_slot_
    };
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

Transition SlotPickHost::pick(const OptionItem& it, std::string_view path, std::string_view label) {
    TASTY_SEAT_BODY(SlotPickHost);
    if (path.empty()) {
        ++refusals_;
        return Transition{};
    }
    arm_(it);
    BrowserEntry entry;
    entry.altname.assign(label);
    return on_picked(entry, path);
}

Transition SlotPickHost::eject(const OptionItem& it) {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!b_.asks || !it.mounts_image()) {
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
        && !b_.names->save_path(env_.file_stem, app::NameConfig::Slot::Mount, pending_slot_, {})) {
        ++refusals_;
    }
    ask_mount(pending_slot_, {});
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
        const auto slot = pending_mounts_ ? app::NameConfig::Slot::Mount
                                          : app::NameConfig::Slot::File;
        if (!b_.names->save_path(env_.file_stem, slot, pending_slot_, path))
            ++refusals_;
    }

    if (pending_mounts_) {
        ask_mount(pending_slot_, path);
        return Transition{Transition::Kind::CloseAll};
    }

    const app::UiRequest::LoadFile ask{
        .index = proto::IoIndex{pending_slot_},
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

Transition SlotPickHost::on_cancel() {
    TASTY_SEAT_BODY(SlotPickHost);
    return Transition{Transition::Kind::Pop};
}

Transition SlotPickHost::on_eject() {
    TASTY_SEAT_BODY(SlotPickHost);
    if (!pending_mounts_) {
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
    if (!b_.names->save_path(env_.file_stem, app::NameConfig::Slot::File, pending_slot_, {})) {
        ++refusals_;
    }
    if (b_.info)
        b_.info->info(app::InfoId::OptionCleared);
    return Transition{Transition::Kind::CloseAll};
}

}
