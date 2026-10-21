// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/content_pick.h"

#include "app/mount_queue.h"
#include "app/owner_push.h"
#include "app/negotiated_core.h"
#include "app/owner_ladder.h"
#include "svc/dir_entry.h"
#include "svc/vfs.h"

namespace mister::app {

ContentPick::ContentPick(const Wiring& w) noexcept
    : inbox_(w.inbox),
      ftx_level_cell_(w.ftx_level_cell),
      owner_events_(w.events),
      vfs_(w.vfs),
      window_map_(w.window_map),
      negotiated_(w.negotiated),
      mounts_(w.mounts),
      ladder_(w.ladder),
      rungs_(w.rungs),
      owner_tally_(w.owner_tally),
      mgl_row0_cell_(w.mgl_row0_cell),
      act_keys_(w.act_keys),
      status_cell_(w.status_cell),
      boot_restore_owed_(w.boot_restore_owed),
      aperture_(w.aperture),
      windows_(w.windows) {}

Ex<proto::ConfStrFileRow> ContentPick::resolve_load_(const UiRequest::LoadFile& r) const {
    if (!negotiated_.conf_str())
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});

    if (!negotiated_.in_scope(r.scope))
        return std::unexpected(Error{Errc::stale, ERR_SITE(), r.scope.gen.v});
    const auto row = negotiated_.conf_str()->menu_pick(r.item, r.index);
    if (!row)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.item.v});
    return *row;
}

Ex<proto::ConfStrFileRow> ContentPick::resolve_load_(const UiRequest::LoadFileByDigit& r) {

    if (negotiated_.conf_str() && !negotiated_.in_scope(r.scope))
        return std::unexpected(Error{Errc::stale, ERR_SITE(), r.scope.gen.v});
    const proto::FileSlotHit hit = negotiated_.conf_str()
        ? negotiated_.conf_str()->find_load_slot(r.digit)
        : proto::FileSlotHit{};
    if (!hit.entry)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.digit.v});

    if (hit.how == proto::FileSlotMatch::RowZero)
        mgl_row0_cell_.publish(++owner_tally_.mgl_row0);
    return proto::ConfStr::row_of(*hit.entry);
}

template <class R>
ContentPick::Out ContentPick::load_file(
    UiRequest::Kind asked,
    const R& req,
    CorrelationTag tag,
    const cores::RamImageRecipe& ram_image
) {
    TASTY_SEAT_BODY(ContentPick);
    if (req.path.empty())
        return Out{.refuse = Errc::bad_format};

    const auto row = resolve_load_(req);

    if (!negotiated_.core_live())
        return Out{.refuse = Errc::core_load};
    if (!negotiated_.conf_str() || boot_restore_owed_)
        return Out{.refuse = Errc::negotiation};
    if (!row)
        return Out{.refuse = row.error().code};
    auto cr = content_for(*row, req.path.view());
    if (!cr)
        return Out{.refuse = Errc::bad_format};
    cr->asked = asked;
    cr->tag = tag;
    UiRequest::SaveChoice choice = UiRequest::SaveChoice::User;
    if constexpr (requires { req.save; })
        choice = req.save;
    const PickedLoad load{.content = *cr, .opensave = row->opensave, .choice = choice, .tag = tag};
    plan_addons_(*row, req.path.view(), load, ram_image);
    if (!picked_load_)
        return Out{.run = load};
    return {};
}

void ContentPick::plan_addons_(
    const proto::ConfStrFileRow& row,
    std::string_view pick,
    const PickedLoad& load,
    const cores::RamImageRecipe& image
) {
    AddonSend addons = AddonSend::plan(row.addon, pick, load.content.slot);
    if (image.size() != 0 && !addons.take_image(cores::expand(image))) {
        ++owner_tally_.ram_images_declined;
        (void)owner_events_.push(
            infra::make<Event>(
                Event::RamImageDeclined{.why = Errc::negotiation},
                Event::Head{EmitSite{ERR_SITE()}, {}, load.tag}
            )
        );
    }
    if (addons.empty())
        return;
    if (row.addon_after) {
        rungs_.addons_after.emplace(std::move(addons));
        return;
    }
    rungs_.addons.emplace(std::move(addons));
    picked_load_ = load;
}

ContentPick::After ContentPick::run_attached_load(const PickedLoad& load, Host& host) {
    TASTY_SEAT_BODY(ContentPick);
    if (negotiated_.owns_loader() && arm_loader(load.content))
        return {};
    return order_row_content(load.content, load.opensave, load.choice, load.tag, host);
}

std::optional<ContentRequest> ContentPick::content_for(
    const proto::ConfStrFileRow& row,
    std::string_view path
) const {
    TASTY_SEAT_BODY(ContentPick);
    ContentRequest cr{};
    if (!cr.path.assign(path))
        return std::nullopt;
    cr.slot = static_cast<std::uint8_t>(proto::ConfStr::wire_index(row, path));
    if (const auto member = zip_member_(row, path)) {
        cr.path = *member;
        cr.slot = static_cast<std::uint8_t>(proto::ConfStr::wire_index(row, member->view()));
    }
    cr.load_addr = row.load_addr;
    return cr;
}

ContentPick::After ContentPick::order_row_content(
    const ContentRequest& cr,
    bool opensave,
    UiRequest::SaveChoice choice,
    CorrelationTag tag,
    Host& host
) {
    TASTY_SEAT_BODY(ContentPick);
    if (opensave && host.arm_open_save(cr, choice, tag))
        return {};
    return launch_content(cr);
}

ContentPick::After ContentPick::launch_content(const ContentRequest& cr) {
    TASTY_SEAT_BODY(ContentPick);
    if (negotiated_.owns_loader() || !arm_loader(cr))
        return perform_content(cr);
    return {};
}

bool ContentPick::wants_zip_member(std::string_view path, std::string_view exts) {
    return !exts.empty()
        && svc::Vfs::extension_matches(path, "ZIP")
        && !svc::Vfs::extension_matches(path, exts);
}

std::optional<PathText> ContentPick::bare_zip_member(
    std::string_view path,
    std::string_view exts,
    std::span<const svc::DirEntry> listing
) {
    TASTY_SEAT_BODY(ContentPick);
    if (!wants_zip_member(path, exts))
        return std::nullopt;
    const auto file = std::ranges::find_if(listing, [exts](const svc::DirEntry& e) {
        return !e.is_dir && svc::Vfs::extension_matches(e.name, exts);
    });
    PathText out{};
    if (file == listing.end() || !out.assign(path) || !out.append("/") || !out.append(file->name)) {
        return std::nullopt;
    }
    return out;
}

std::optional<PathText> ContentPick::zip_member_(
    const proto::ConfStrFileRow& row,
    std::string_view path
) const {
    if (!wants_zip_member(path, row.ext))
        return std::nullopt;
    const auto members = vfs_.scan(
        path,
        svc::ScanFilter{.extensions = row.ext, .directories = false}
    );
    if (!members)
        return std::nullopt;
    return bare_zip_member(path, row.ext, *members);
}

ContentPick::After ContentPick::perform_content(const ContentRequest& req) {
    TASTY_SEAT_BODY(ContentPick);
    content_owed_ = Owed{req.asked, req.tag};
    if (req.path.empty()) {
        ++owner_tally_.files_failed;
        return {};
    }
    const std::string_view path = req.path.view();
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) {
        ++owner_tally_.files_failed;
        return {};
    }

    const bool quiet = !req.whole && no_mount_in_flight_();
    if (sz->v > InternedFile::kPieceBytes && quiet && streams_files_()) {
        auto p = FileTxPieces::start(std::move(*f), sz->v, req.slot, path);
        if (!p) {
            ++owner_tally_.files_failed;
            return {};
        }
        forget_watch();
        rungs_.pieces.emplace(std::move(*p));
        return {};
    }
    auto buf = read_whole(**f, sz->v);
    if (!buf) {
        ++owner_tally_.files_failed;
        return {};
    }
    const std::size_t dot = path.rfind('.');
    const std::string_view ext = dot == std::string_view::npos ? std::string_view{}
                                                               : path.substr(dot);
    LoadKey key(buf->size());
    key.feed(*buf);
    const auto id = inbox_.intern_file(std::move(*buf), ext, path, req.load_addr, key.value());
    if (!id) {
        ++owner_tally_.files_failed;
        return {};
    }
    if (req.save != proto::FileId{} && !inbox_.stamp_save(*id, req.save))
        ++owner_tally_.open_saves_skipped;
    if (req.edge != ContentEdge::Content)
        (void)inbox_.stamp_edge(*id, req.edge);

    ContentWatch watch{
        .file = std::move(*f),
        .total = sz->v,
        .crc = key.value(),
        .wire_index = req.slot,
        .cut_on_hold = req.save != proto::FileId{} || req.edge != ContentEdge::Content
    };
    if (!watch.path.assign(path))
        watch.file.reset();
    if (!order_content_tx(*id, std::move(watch))) {
        ++owner_tally_.op_drops;
        return {};
    }
    ++owner_tally_.ops_posted;
    ++owner_tally_.files_loaded;
    publish_done_(req.asked, req.tag);
    if (req.load_addr == 0)
        return After{.companion = req.path};
    return {};
}

bool ContentPick::order_content_tx(proto::FileId file, ContentWatch watch) {
    TASTY_SEAT_BODY(ContentPick);
    watch.act = act_keys_.next();
    const proto::LinkOp::FileTx op{.wire_index = watch.wire_index, .file = file, .act = watch.act};
    if (!inbox_.push(op))
        return false;
    watch_.emplace(std::move(watch));
    return true;
}

bool ContentPick::watch_answered() const noexcept {
    TASTY_SEAT_BODY(ContentPick);
    if (!watch_)
        return false;
    const auto s = ftx_level_cell_.sample();
    return s && s.value.answers(watch_->act);
}

void ContentPick::forget_watch() noexcept {
    TASTY_SEAT_BODY(ContentPick);
    if (!watch_)
        return;
    watch_.reset();
    ++tally_.long_wait_drops;
}

void ContentPick::step_watch() {
    TASTY_SEAT_BODY(ContentPick);
    if (!watch_)
        return;
    const auto s = ftx_level_cell_.sample();
    if (!s || !s.value.answers(watch_->act))
        return;
    const FileTxLevel& l = s.value;
    if (l.held_act != watch_->act) {
        watch_.reset();
        return;
    }

    if (rungs_.pieces || rungs_.load || rungs_.walk || rungs_.companion) {
        forget_watch();
        return;
    }
    ContentWatch& w = *watch_;
    if (!w.cut_on_hold && w.file) {
        auto p = FileTxPieces::resume(
            std::move(w.file),
            w.total,
            w.wire_index,
            w.path.view(),
            w.from + l.held_bytes,
            w.crc
        );
        if (p) {
            rungs_.pieces.emplace(std::move(*p));
            ++tally_.long_wait_resumes;
            watch_.reset();
            return;
        }
    }
    const proto::LinkOp::FileTx cut{
        .wire_index = w.wire_index,
        .phase = proto::LinkOp::FileTxPhase::Close,
        .total = static_cast<std::uint32_t>(w.total)
    };
    if (!push_posted(inbox_, owner_tally_, cut))
        return;
    ++owner_tally_.files_failed;
    ++tally_.long_wait_refusals;
    watch_.reset();
}

void ContentPick::watch_last_piece() {
    TASTY_SEAT_BODY(ContentPick);
    const FileTxPieces::LastOp l = rungs_.pieces->last_op();
    ContentWatch w{
        .act = l.act,
        .file = rungs_.pieces->take_file(),
        .total = rungs_.pieces->total(),
        .crc = l.crc,
        .wire_index = rungs_.pieces->wire_index(),
        .from = l.from,
        .cut_on_hold = l.shaped
    };
    if (!w.path.assign(rungs_.pieces->path()))
        w.file.reset();
    watch_.emplace(std::move(w));
}

bool ContentPick::arm_loader(
    const ContentRequest& req,
    std::string_view save,
    std::uint64_t save_bytes
) {
    TASTY_SEAT_BODY(ContentPick);
    const cores::CoreFactory* row = negotiated_.load_row();
    const cores::MakeLoader make = cores::loader_for(row, req.load_addr);
    if (make == nullptr)
        return false;
    const bool own = row != nullptr && row->make_loader != nullptr;

    if (!own && (rungs_.load || rungs_.walk || rungs_.pieces)) {
        ++owner_tally_.files_failed;
        return true;
    }
    content_owed_ = Owed{req.asked, req.tag};
    if (own && cores::walks_romset(row->profile->slots, proto::IoIndex{req.slot})) {
        return arm_walk_(req, *row);
    }
    const cores::CoreProfile& profile = row != nullptr
        ? *row->profile
        : cores::profile_for(negotiated_.core_name().view());
    const std::string_view path = req.path.view();
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) {
        ++owner_tally_.files_failed;
        return true;
    }
    const bool turbo = negotiated_.conf_str() && negotiated_.conf_str()->declares_turbo();
    auto loader = make(cores::LoaderContext{vfs_, profile, turbo, loader_memo_, aperture_});
    const cores::LoadAsk ask{
        .index = proto::IoIndex{req.slot},
        .path = path,
        .size = sz->v,
        .load_addr = req.load_addr
    };
    const auto plan = loader->plan(ask, status_cell_.sample().value);
    if (!plan || plan->count == 0) {
        ++owner_tally_.files_failed;
        return true;
    }

    if (plan->count > 1) {
        auto w = LoadWalk::start(std::move(loader), *plan, profile.windows);
        if (!w) {
            ++owner_tally_.files_failed;
            return true;
        }
        forget_watch();
        rungs_.walk.emplace(std::move(*w));
        return true;
    }
    const cores::TransferRow& r = plan->rows[0];
    const bool plain = !r.shaped && !r.placed;
    if (plain && r.dest != cores::RowDest::Window)
        return false;
    if (r.dest == cores::RowDest::Window) {
        const std::uint64_t extent = std::max<std::uint64_t>(sz->v, r.extent);
        auto w = LoadWindow<seat_owner::Main>::open(
            window_map_,
            aperture_,
            os::PhysAddr{r.addr},
            extent
        );
        std::optional<LoadWindow<seat_owner::Main>> mirror{};
        if (w && r.mirror != 0) {
            auto m = LoadWindow<seat_owner::Main>::open(
                window_map_,
                aperture_,
                os::PhysAddr{r.mirror},
                extent
            );
            if (m) {
                mirror.emplace(std::move(*m));
            } else {
                w = std::unexpected(m.error());
            }
        }
        if (w) {
            LoadLadder::Job job{
                .loader = std::move(loader),
                .file = std::move(*f),
                .total = sz->v,
                .wire_index = req.slot,
                .act = act_keys_.next(),
                .row = r,
                .mirror = std::move(mirror),
                .save_bytes = save_bytes
            };
            if (!job.save.assign(save)) {
                ++owner_tally_.files_failed;
                return true;
            }
            auto l = LoadLadder::start(std::move(job), std::move(*w), path);
            if (!l) {
                ++owner_tally_.files_failed;
                return true;
            }
            forget_watch();
            rungs_.load.emplace(std::move(*l));
            return true;
        }
        windows_.refused(w.error().code);
        windows_.publish();

        if (r.placed || r.mirror != 0) {
            ++owner_tally_.files_failed;
            return true;
        }

        if (plain)
            return false;
    }
    auto p = FileTxPieces::start(std::move(*f), sz->v, req.slot, path, std::move(loader), r);
    if (!p) {
        ++owner_tally_.files_failed;
        return true;
    }
    forget_watch();
    rungs_.pieces.emplace(std::move(*p));
    return true;
}

bool ContentPick::arm_walk_(const ContentRequest& req, const cores::CoreFactory& row) {
    const bool turbo = negotiated_.conf_str() && negotiated_.conf_str()->declares_turbo();
    auto loader = row.make_loader(
        cores::LoaderContext{vfs_, *row.profile, turbo, loader_memo_, aperture_}
    );
    const cores::LoadAsk ask{
        .index = proto::IoIndex{req.slot},
        .path = req.path.view(),
        .load_addr = req.load_addr
    };
    const auto plan = loader->plan(ask, status_cell_.sample().value);
    auto w = plan ? LoadWalk::start(std::move(loader), *plan, row.profile->windows)
                  : Ex<LoadWalk>{std::unexpected(plan.error())};
    if (!w) {
        ++owner_tally_.files_failed;
        return true;
    }
    forget_watch();
    rungs_.walk.emplace(std::move(*w));
    return true;
}

template ContentPick::Out ContentPick::load_file(
    UiRequest::Kind,
    const UiRequest::LoadFile&,
    CorrelationTag,
    const cores::RamImageRecipe&
);
template ContentPick::Out ContentPick::load_file(
    UiRequest::Kind,
    const UiRequest::LoadFileByDigit&,
    CorrelationTag,
    const cores::RamImageRecipe&
);

bool ContentPick::streams_files_() const {
    return !negotiated_.loaded_profile().file_tx_whole;
}

bool ContentPick::no_mount_in_flight_() const noexcept {
    return !ladder_.live() && !mounts_.save_mount_live() && !mounts_.mount_held();
}

void ContentPick::publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept {
    if (tag == kUncaused)
        return;
    (void)owner_events_.push(
        infra::make<Event>(
            Event::RequestDone{.which = which},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

}
