// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/content_pick.h"

#include "app/mount_queue.h"
#include "app/owner_push.h"
#include "app/negotiated_core.h"
#include "app/owner_ladder.h"
#include "infra/path_parts.h"
#include "svc/dir_entry.h"
#include "svc/vfs.h"

namespace mister::app {

ContentPick::ContentPick(const Wiring& w) noexcept
    : inbox_(w.inbox),
      ftx_level_cell_(w.ftx_level_cell),
      owner_events_(w.events.producer()),
      vfs_(w.vfs),
      window_map_(w.window_map),
      negotiated_(w.negotiated),
      mounts_(w.mounts),
      ladder_(w.ladder),
      belt_(w.belt),
      owner_tally_(w.owner_tally),
      act_keys_(w.act_keys),
      status_cell_(w.status_cell),
      boot_restore_(w.boot_restore),
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
        ? negotiated_.conf_str()->file_slots().load(r.digit)
        : proto::FileSlotHit{};
    if (!hit.entry)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.digit.slot().detail()});

    if (hit.how == proto::FileSlotMatch::RowZero)
        owner_tally_.mgl_row0.add();
    return hit.entry->wire_row();
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
    if (!negotiated_.conf_str() || boot_restore_ != BootRestore::None)
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
    BeltOrders orders = plan_addons_(*row, req.path.view(), load, ram_image);
    if (!picked_load_)
        return Out{.run = load, .orders = std::move(orders)};
    return Out{.orders = std::move(orders)};
}

BeltOrders ContentPick::plan_addons_(
    const proto::ConfStrFileRow& row,
    std::string_view pick,
    const PickedLoad& load,
    const cores::RamImageRecipe& image
) {
    AddonSend addons = AddonSend::plan(row.addon, pick, load.content.slot);
    if (image.size() != 0 && !addons.take_image(cores::expand(image))) {
        ++owner_tally_.ram_images_declined;
        owner_events_.push(
            Event::RamImageDeclined{.why = Errc::negotiation},
            Event::Head{ERR_SITE(), {}, load.tag}
        );
    }
    if (addons.empty())
        return {};
    if (row.addon_after)
        return BeltOrders{.addons_after = std::move(addons)};
    picked_load_ = load;
    return BeltOrders{.addons = std::move(addons)};
}

ContentPick::After ContentPick::run_attached_load(const PickedLoad& load, Host& host) {
    TASTY_SEAT_BODY(ContentPick);
    if (negotiated_.owns_loader()) {
        Armed a = arm_loader(load.content);
        if (a.busy)
            host.refuse_busy(load.content.asked, load.content.tag);
        if (a.taken)
            return After{.orders = std::move(a.orders)};
    }
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
    cr.slot = row.wire_index(path);
    if (const auto member = zip_member_(row, path)) {
        cr.path = *member;
        cr.slot = row.wire_index(member->view());
    }
    cr.load_addr = hal::FabricAddr{row.load_addr};
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
    if (negotiated_.owns_loader())
        return perform_content(cr);
    Armed a = arm_loader(cr);
    if (!a.taken)
        return perform_content(cr);
    return After{.orders = std::move(a.orders)};
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
    auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) {
        ++owner_tally_.files_failed;
        return {};
    }

    const auto row = negotiated_.load_row();
    const bool derived = row && row->derives_content;
    if (derived) {
        auto made = derive_(req, std::move(*f), *sz);
        if (!made) {
            ++owner_tally_.files_failed;
            return {};
        }
        f = std::move(*made);
        auto made_size = (*f)->size();
        if (!made_size) {
            ++owner_tally_.files_failed;
            return {};
        }
        sz = std::move(made_size);
    }

    const bool quiet = !req.whole && no_mount_in_flight_();
    if (*sz > proto::FileSize{InternedFile::kPieceBytes} && quiet && streams_files_()) {
        auto p = FileTxPieces::start(std::move(*f), sz->v, req.slot, path);
        if (!p) {
            ++owner_tally_.files_failed;
            return {};
        }
        forget_watch();
        return After{.orders = {.pieces = std::move(*p)}};
    }

    if (derived && !quiet && *sz > kDerivedWholeMax) {
        ++owner_tally_.files_failed;
        return {};
    }

    if (derived && sz->empty()) {
        if (!order_empty_bracket_(req, path)) {
            ++owner_tally_.op_drops;
            return {};
        }
        ++owner_tally_.files_loaded;
        publish_done_(req.asked, req.tag);
        if (req.load_addr == hal::FabricAddr{})
            return After{.companion = req.path};
        return {};
    }
    auto buf = read_whole(**f, *sz);
    if (!buf) {
        ++owner_tally_.files_failed;
        return {};
    }
    const std::string_view ext = infra::extension(path);
    const std::uint64_t payload_bytes = buf->size();
    LoadKey key(buf->size());
    key.feed(*buf);
    const auto id = inbox_.intern_file(
        FileIntern{
            .bytes = std::move(*buf),
            .ext = ext,
            .path = path,
            .load_addr = req.load_addr,
            .crc = key.value(),
            .save = req.save,
            .edge = req.edge
        }
    );
    if (!id) {
        ++owner_tally_.files_failed;
        return {};
    }

    ContentWatch watch{
        .file = std::move(*f),
        .total = payload_bytes,
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
    ++owner_tally_.files_loaded;
    publish_done_(req.asked, req.tag);
    if (req.load_addr == hal::FabricAddr{})
        return After{.companion = req.path};
    return {};
}

cores::LoaderContext ContentPick::loader_context_() {
    TASTY_SEAT_BODY(ContentPick);
    const bool turbo = negotiated_.conf_str() && negotiated_.conf_str()->declares_turbo();
    return cores::LoaderContext{
        vfs_,
        negotiated_.loaded_profile(),
        turbo,
        loader_memo_,
        aperture_,
        infra::OptRef<const proto::StatusCell>{status_cell_}
    };
}

std::optional<std::unique_ptr<svc::IFile>> ContentPick::derive_(
    const ContentRequest& req,
    std::unique_ptr<svc::IFile> raw,
    proto::FileSize size
) {
    TASTY_SEAT_BODY(ContentPick);
    const auto row = negotiated_.load_row();
    if (!row || row->make_loader == nullptr)
        return std::nullopt;
    auto loader = row->make_loader(loader_context_());
    const cores::LoadAsk
        ask{.index = req.slot, .path = req.path.view(), .size = size, .load_addr = req.load_addr};
    auto made = loader->source(ask, std::move(raw));
    if (!made)
        return std::nullopt;
    return std::optional<std::unique_ptr<svc::IFile>>{std::move(*made)};
}

bool ContentPick::order_empty_bracket_(const ContentRequest& req, std::string_view path) {
    TASTY_SEAT_BODY(ContentPick);
    auto id = inbox_.intern_empty_file(
        FileIntern{.path = path, .save = req.save, .edge = req.edge}
    );
    if (!id)
        return false;
    return order_content_tx(
        *id,
        ContentWatch{.total = 0, .wire_index = req.slot},
        proto::LinkOp::FileTxPhase::Empty
    );
}

bool ContentPick::order_content_tx(
    proto::FileId file,
    ContentWatch watch,
    proto::LinkOp::FileTxPhase phase
) {
    TASTY_SEAT_BODY(ContentPick);
    watch.act = act_keys_.next();
    const proto::LinkOp::FileTx op{
        .wire_index = watch.wire_index.fio_byte(),
        .phase = phase,
        .file = file,
        .act = watch.act
    };
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

BeltOrders ContentPick::step_watch() {
    TASTY_SEAT_BODY(ContentPick);
    if (!watch_)
        return {};
    const auto s = ftx_level_cell_.sample();
    if (!s || !s.value.answers(watch_->act))
        return {};
    const FileTxLevel& l = s.value;
    if (l.held_act != watch_->act) {
        watch_.reset();
        return {};
    }

    if (belt_.any_live(kWatchCutters)) {
        forget_watch();
        return {};
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
            ++tally_.long_wait_resumes;
            watch_.reset();
            return BeltOrders{.pieces = std::move(*p)};
        }
    }
    const proto::LinkOp::FileTx cut{
        .wire_index = w.wire_index.fio_byte(),
        .phase = proto::LinkOp::FileTxPhase::Close,
        .total = static_cast<std::uint32_t>(w.total)
    };
    if (!inbox_.push(cut))
        return {};
    ++owner_tally_.files_failed;
    ++tally_.long_wait_refusals;
    watch_.reset();
    return {};
}

void ContentPick::watch_last_piece(FileTxPieces& pieces) {
    TASTY_SEAT_BODY(ContentPick);
    const FileTxPieces::LastOp l = pieces.last_op();
    ContentWatch w{
        .act = l.act,
        .file = pieces.take_file(),
        .total = pieces.total(),
        .crc = l.crc,
        .wire_index = pieces.wire_index(),
        .from = l.from,
        .cut_on_hold = l.shaped
    };
    if (!w.path.assign(pieces.path()))
        w.file.reset();
    watch_.emplace(std::move(w));
}

ContentPick::Armed ContentPick::arm_loader(
    const ContentRequest& req,
    std::string_view save,
    proto::FileSize save_bytes
) {
    TASTY_SEAT_BODY(ContentPick);
    const auto row = negotiated_.load_row();

    if (row && row->derives_content)
        return {};
    const cores::MakeLoader make = cores::loader_for(row, req.load_addr);
    if (make == nullptr)
        return {};
    const bool own = row && row->make_loader != nullptr;

    if (own && belt_.any_live(kLoaderBusy))
        return Armed{.taken = true, .busy = true};
    if (!own && belt_.any_live(kLoaderBusy)) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }
    content_owed_ = Owed{req.asked, req.tag};
    if (own && cores::walks_romset(row->profile.slots, req.slot.io())) {
        return arm_walk_(req, *row);
    }
    const cores::CoreProfile& profile = negotiated_.loaded_profile();
    const std::string_view path = req.path.view();
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }
    auto loader = make(loader_context_());
    const cores::LoadAsk
        ask{.index = req.slot, .path = path, .size = *sz, .load_addr = req.load_addr};
    const auto plan = loader->plan(ask, status_cell_.sample().value);
    if (!plan || plan->count == 0) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }

    if (plan->count > 1) {
        auto w = LoadWalk::start(std::move(loader), *plan, profile.windows);
        if (!w) {
            ++owner_tally_.files_failed;
            return Armed{.taken = true};
        }
        forget_watch();
        return Armed{.taken = true, .orders = {.walk = std::move(*w)}};
    }
    const cores::TransferRow& r = plan->rows[0];
    const bool plain = !r.shaped && !r.placed;
    if (plain && r.dest != cores::RowDest::Window)
        return {};
    if (r.dest == cores::RowDest::Window) {
        auto armed = arm_window_row_(r, *f, loader, *sz, req, save, save_bytes);
        if (armed)
            return std::move(*armed);

        if (plain)
            return {};
    }
    auto p = FileTxPieces::start(std::move(*f), sz->v, req.slot, path, std::move(loader), r);
    if (!p) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }
    forget_watch();
    return Armed{.taken = true, .orders = {.pieces = std::move(*p)}};
}

std::optional<ContentPick::Armed> ContentPick::arm_window_row_(
    const cores::TransferRow& row,
    std::unique_ptr<svc::IFile>& file,
    std::unique_ptr<cores::ILoader>& loader,
    svc::FileSize size,
    const ContentRequest& req,
    std::string_view save,
    proto::FileSize save_bytes
) {
    const std::uint64_t extent = std::max<std::uint64_t>(
        size.v,
        row.extent.file_word<std::size_t>()
    );
    auto w = LoadWindow<seat_owner::Main>::open(
        window_map_,
        aperture_,
        os::PhysAddr{row.addr},
        extent
    );
    std::optional<LoadWindow<seat_owner::Main>> mirror{};
    if (w && row.mirror != 0) {
        auto m = LoadWindow<seat_owner::Main>::open(
            window_map_,
            aperture_,
            os::PhysAddr{row.mirror},
            extent
        );
        if (m) {
            mirror.emplace(std::move(*m));
        } else {
            w = std::unexpected(m.error());
        }
    }
    if (!w) {
        windows_.refused(w.error().code);
        windows_.publish();

        if (row.placed || row.mirror != 0) {
            ++owner_tally_.files_failed;
            return Armed{.taken = true};
        }
        return std::nullopt;
    }
    LoadLadder::Job job{
        .loader = std::move(loader),
        .file = std::move(file),
        .total = size.v,
        .wire_index = req.slot,
        .act = act_keys_.next(),
        .row = row,
        .mirror = std::move(mirror),
        .save_bytes = save_bytes
    };
    if (!job.save.assign(save)) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }
    auto l = LoadLadder::start(std::move(job), std::move(*w), req.path.view());
    if (!l) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }
    forget_watch();
    return Armed{.taken = true, .orders = {.load = std::move(*l)}};
}

ContentPick::Armed ContentPick::arm_walk_(
    const ContentRequest& req,
    const cores::CoreFactory& row
) {
    auto loader = row.make_loader(loader_context_());
    const cores::LoadAsk ask{
        .index = req.slot,
        .path = req.path.view(),
        .load_addr = req.load_addr
    };
    const auto plan = loader->plan(ask, status_cell_.sample().value);
    auto w = plan ? LoadWalk::start(std::move(loader), *plan, row.profile.windows)
                  : Ex<LoadWalk>{std::unexpected(plan.error())};
    if (!w) {
        ++owner_tally_.files_failed;
        return Armed{.taken = true};
    }
    forget_watch();
    return Armed{.taken = true, .orders = {.walk = std::move(*w)}};
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
    owner_events_.push(Event::RequestDone{.which = which}, Event::Head{ERR_SITE(), {}, tag});
}

}
