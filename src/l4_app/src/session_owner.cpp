// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_owner.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#include <chrono>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "app/xml_kind.h"
#include "app/cheat_catalog_pack.h"
#include "app/conf_str_cell.h"
#include "app/durable_write.h"
#include "app/durable_write_service.h"
#include "app/ini_key_edit.h"
#include "app/ini_parse.h"
#include "app/link_event_dispatch.h"
#include "app/link_rows.h"
#include "app/mra_facts.h"
#include "app/pending_load.h"
#include "app/remembered_path.h"
#include "app/sdram_size_code.h"
#include "app/stream_io.h"
#include "app/ui_request_dispatch.h"
#include "app/config_apply.h"
#include "app/event.h"
#include "cores/boot_asset.h"
#include "cores/cheat_records.h"
#include "cores/ladder_context.h"
#include "cores/payload_pieces.h"
#include "cores/addressed_load.h"
#include "cores/manifest_doc_role.h"
#include "cores/registry.h"
#include "cores/staging_core.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "hal/boot_handoff.h"
#include "proto/conf_str.h"
#include "proto/core_session.h"
#include "proto/link_op.h"
#include "proto/link_op.h"
#include "svc/file.h"
#include "svc/search_policy.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

bool mount_present(std::string_view needle) {
    if (needle.empty()) return true;
    const int fd = ::open("/proc/mounts", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::string text;
    char buf[4096];
    for (;;) {
        const ::ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        text.append(buf, static_cast<std::size_t>(n));
        if (text.size() > (1u << 20)) break;
    }
    (void)::close(fd);
    return text.find(needle) != std::string::npos;
}

}  // namespace

constexpr std::int64_t kRebootDrainBoundNs = 5'000'000'000;

constexpr std::int64_t kPauseDeadlineNs = 1'000'000'000;

constexpr std::int64_t kSaveMountBoundNs = cores::kDiscMountBoundNs;

constexpr int kLastOrderRetryMs = 1;

constexpr int kPieceRetryMs = 5;

static_assert(cores::PayloadPieces::kAhead + InternedFile::kHeldOps + 1 <
              InternedFile::kMaxLiveFiles);

namespace {

constexpr bool pauses_at_receipt(SeatTag s) noexcept { return s == SeatTag::Io; }

bool wants_zip_member(std::string_view path, std::string_view exts) {
    return !exts.empty() && svc::Vfs::extension_matches(path, "ZIP") &&
           !svc::Vfs::extension_matches(path, exts);
}
}  // namespace

constexpr std::size_t kTeardownOps = 3;
static_assert(proto::kLinkTxCapacity >= kTeardownOps);

unsigned SessionOwner::tick() {

    builder_.reap();

    if (watch_reboot_()) {
        publish_fabric_();
        return 0;
    }

    (void)push_last_order_();

    retry_held_load_();

    step_ladder_();

    step_savestate_hold_();
    step_save_mount_();
    step_addons_();
    step_watch_();
    step_pieces_();
    step_load_();
    step_manifest_();
    step_walk_();
    step_companion_();

    const unsigned acks = take_park_acks_();
    try_program_();

    watch_start_();
    fall_back_to_front_end_();
    swap_to_launcher_image_();

    watch_readiness_();

    publish_fabric_();
    return acks;
}

void SessionOwner::on(const UiRequest::LoadCore& req, const UiRequest::Head& head) {
    on_load_core_(launcher_alias_(req), head.tag);
}

void SessionOwner::on(const UiRequest::SaveConfig&, const UiRequest::Head& head) {
    on_save_config_(head.tag);
}

void SessionOwner::on(const UiRequest::SaveDips&, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::SaveDips::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::SaveDips::kKind, head.tag)) return;
    if (!order_save_ask_(proto::SaveKind::Dips, 0, head.tag)) {
        publish_save_verdict_(false, head.tag);
    }
}

void SessionOwner::on(const UiRequest::SaveCoreConfig& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::SaveCoreConfig::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::SaveCoreConfig::kKind, head.tag)) return;

    if (!in_scope_(req.scope))
        return publish_refusal_(UiRequest::SaveCoreConfig::kKind, Errc::stale, head.tag);
    slot_save_slot_ = req.slot.v;
    if (!order_save_ask_(proto::SaveKind::Slot, req.slot.v, head.tag)) {
        publish_save_verdict_(false, head.tag);
    }
}

void SessionOwner::on(const UiRequest::LoadCoreConfig& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::LoadCoreConfig::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::LoadCoreConfig::kKind, head.tag)) return;
    if (!in_scope_(req.scope))
        return publish_refusal_(UiRequest::LoadCoreConfig::kKind, Errc::stale, head.tag);
    slot_load_slot_ = req.slot.v;
    (void)order_save_ask_(proto::SaveKind::SlotNames, req.slot.v, head.tag);
}

void SessionOwner::on(const UiRequest::LoadFile& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::LoadFile::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::LoadFile::kKind, head.tag)) return;
    load_file_(UiRequest::LoadFile::kKind, req, head.tag);
}

void SessionOwner::on(const UiRequest::LoadFileByDigit& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::LoadFileByDigit::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::LoadFileByDigit::kKind, head.tag)) return;
    load_file_(UiRequest::LoadFileByDigit::kKind, req, head.tag);
}

void SessionOwner::on(const UiRequest::LoadRamImage& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::LoadRamImage::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::LoadRamImage::kKind, head.tag)) return;
    if (!core_live_)
        return refuse_file_ask_(UiRequest::LoadRamImage::kKind, head.tag, Errc::core_load);
    if (!conf_str_ || !in_scope_(req.scope))
        return refuse_file_ask_(UiRequest::LoadRamImage::kKind, head.tag, Errc::stale);
    if (req.recipe.size() == 0) {
        ++ram_images_declined_;
        (void)owner_events_.push(
            infra::make<Event>(Event::RamImageDeclined{.why = Errc::bad_format},
                               Event::Head{EmitSite{ERR_SITE()}, {}, head.tag}));
        return;
    }

    pending_ram_image_ = req.recipe;
}

void SessionOwner::on(const UiRequest::MountImage& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::MountImage::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::MountImage::kKind, head.tag)) return;
    if (!in_scope_(req.scope))
        return publish_refusal_(UiRequest::MountImage::kKind, Errc::stale, head.tag);
    on_mount_image_(MountAsk{UiRequest::MountImage::kKind, req.index, req.path, head.tag});
}

void SessionOwner::on(const UiRequest::UnmountImage& req, const UiRequest::Head& head) {
    if (refuse_in_switch_(UiRequest::UnmountImage::kKind, head.tag)) return;
    if (refuse_in_load_(UiRequest::UnmountImage::kKind, head.tag)) return;
    if (!in_scope_(req.scope))
        return publish_refusal_(UiRequest::UnmountImage::kKind, Errc::stale, head.tag);
    on_mount_image_(MountAsk{UiRequest::UnmountImage::kKind, req.index, PathText{}, head.tag});
}

void SessionOwner::on(const UiRequest::SetOsdStyle& req, const UiRequest::Head&) {
    constexpr std::string_view kIni = "MiSTer.ini";
    std::string text;
    auto f = vfs_.open(kIni, svc::OpenMode::ReadWhole);
    if (f) {
        const auto sz = (*f)->size();
        const auto buf = sz ? read_whole_(**f, sz->v) : std::nullopt;
        if (!buf) {
            ini_write_failures_cell_.publish(++ini_write_failures_);
            return;
        }
        text.assign(infra::as_text(std::span(*buf)));
    } else if (!(f.error().code == Errc::os && f.error().detail == ENOENT) &&
               f.error().code != Errc::not_found) {
        ini_write_failures_cell_.publish(++ini_write_failures_);
        return;
    }
    const std::string out = ini_set_key(text, "osd_style", req.style != 0 ? "1" : "0");
    if (!write_durably_(kIni, std::as_bytes(std::span<const char>(out.data(), out.size())))) {
        ini_write_failures_cell_.publish(++ini_write_failures_);
    }
}

void SessionOwner::on(const UiRequest::SdramDetected& req, const UiRequest::Head&) {
    const std::uint8_t code = sdram_size_code(req.osd_mask);

    if (handoff_ && !handoff_->write_sdram_cfg(code)) ++sdram_write_failures_;
    set_boot_cookie(static_cast<std::uint16_t>(kSdramCookieMeasured | code));
}

void SessionOwner::note_memsz_(const UiRequest::LoadCore& req) noexcept {
    if (req.path.empty() || names_front_end_image(req.path.view(), req.xml)) return;
    if ((cookie_ & kSdramCookieMeasured) == 0) ++memsz_unmeasured_;
}

static_assert(UiRequestSink<SessionOwner>);

void SessionOwner::on(const UiRequest::ResetCore& req, const UiRequest::Head& head) {
    constexpr UiRequest::Kind kKind = UiRequest::ResetCore::kKind;
    if (refuse_in_switch_(kKind, head.tag)) return;
    if (!core_live_ || !conf_str_) return publish_refusal_(kKind, Errc::core_load, head.tag);
    if (!in_scope_(req.scope)) return publish_refusal_(kKind, Errc::stale, head.tag);
    if (refuse_in_load_(kKind, head.tag)) return;
    if (ladder_ != nullptr || save_mount_.has_value() || reset_owed_.has_value())
        return publish_refusal_(kKind, Errc::would_block, head.tag);

    const bool clears = req.edge.toggles_reset_bit();
    const proto::LinkOp::CoreReset op{.edge = req.edge};
    const auto row = cores::find_core(core_name_.view());
    if (row && (*row)->profile->reset.ejects(req.edge)) {
        const bool carried = ladder_noreset_;
        if (clears) ladder_noreset_ = false;
        arm_mount_(MountAsk{UiRequest::Kind::UnmountImage, (*row)->profile->staging.disc_slot,
                            PathText{}, head.tag});
        if (ladder_ == nullptr) {
            ladder_noreset_ = carried;
            return;
        }
        reset_owed_ = op;
        return;
    }
    if (order_reset_(op, head.tag) && clears) ladder_noreset_ = false;
}

bool SessionOwner::order_reset_(const proto::LinkOp::CoreReset& op, CorrelationTag tag) noexcept {
    if (!order(op)) {
        publish_refusal_(UiRequest::ResetCore::kKind, Errc::would_block, tag);
        return false;
    }
    ++resets_ordered_;
    return true;
}

void SessionOwner::on(const UiRequest::Reboot&, const UiRequest::Head&) { settle_reboot_(true); }

void SessionOwner::misrouted(const UiRequest&) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++ui_request_misrouted_;
}

bool SessionOwner::refuse_in_switch_(UiRequest::Kind kind, CorrelationTag tag) noexcept {
    if (reboot_owed_) {
        ++reboot_refusals_;
        publish_refusal_(kind, Errc::negotiation, tag);
        return true;
    }
    if (!switch_standing_) return false;
    ++switch_belt_refusals_;
    publish_refusal_(kind, Errc::negotiation, tag);
    return true;
}

bool SessionOwner::refuse_in_load_(UiRequest::Kind kind, CorrelationTag tag) noexcept {
    const bool rom_waits = save_mount_ && save_mount_->then_load;
    if (!pieces_ && !load_ && !walk_ && !companion_walk_ && !rom_waits && !addons_ &&
        !addons_after_ && !ss_hold_)
        return false;
    ++loads_refused_busy_;
    publish_refusal_(kind, Errc::would_block, tag);
    return true;
}

void SessionOwner::watch_readiness_() {

    if (!recovering_ && fabric_unconfigured_()) {
        recover_owed_ = false;
        return;
    }
    if (recover_owed_) {
        if (!switch_standing_) recover_session_();
        return;
    }
    if (recovering_) {
        probe_for_recovery_();
        return;
    }
    hal::PinLevels lv{};

    if (levels_.sample_into(lv) == 0u) return;
    if (lv.core_ready) {
        ready_watch_armed_ = true;
        return;
    }
    if (!ready_watch_armed_) return;
    ready_watch_armed_ = false;
    recover_session_();
}

void SessionOwner::watch_start_() noexcept {

    if (!start_bounded_ || start_bound_ms_ == 0) return;
    if (!start_due_.expired(clock_)) return;
    give_up_start_(Errc::timeout);
}

void SessionOwner::give_up_start_(Errc why) noexcept {

    awaiting_identity_ = false;
    identity_tries_ = 0;
    if (why == Errc::timeout) {
        ++fallbacks_.start_timeouts;
        fallback_cell_.publish(fallbacks_);
    }
    refuse_started_switch_(why);
}

void SessionOwner::owe_front_end_() noexcept {
    if (front_end_fallback_armed_) {
        front_end_owed_ = true;
        return;
    }

    if (names_front_end_image(pending_.path.view(), pending_.xml)) {
        ++fallbacks_.front_end_failed;
        fallback_cell_.publish(fallbacks_);
    }
}

void SessionOwner::fall_back_to_front_end_() {
    if (!front_end_owed_ || before_seats_ || switch_standing_ || recovering_) return;
    if (reboot_failed_) {
        front_end_owed_ = false;
        return;
    }

    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps || !park_.ask_room()) return;
    const UiRequest::LoadCore req{.path = front_end_image_, .xml = XmlKind::Rbf};
    switch (ask_switch_(req, kUncaused)) {
        case Asked::Yes:
            ++fallbacks_.front_end_asked;
            break;
        case Asked::Invalid:
            front_end_owed_ = false;
            ++fallbacks_.front_end_failed;
            break;
        case Asked::NoRoom:
            return;
    }
    fallback_cell_.publish(fallbacks_);
}

bool SessionOwner::fabric_configured_() const noexcept { return programmer_.programmed(); }

bool SessionOwner::fabric_unconfigured_() const noexcept {
    return programmer_.bridges_down() && !load_ok_;
}

void SessionOwner::recover_session_() {

    if (switch_standing_) {
        recover_owed_ = !switch_programmed_;
        return;
    }
    recover_owed_ = false;
    ++recoveries_;

    ask_switch_or_refuse_(pending_, pending_tag_);
    if (!switch_standing_) {
        drop_ladder_();
        forget_core_();
        order_teardown_();
    }
    recovering_ = true;
    recover_tries_ = 0;
    probe_for_recovery_();
}

void SessionOwner::probe_for_recovery_() {
    if (fabric_configured_()) {

        recovering_ = false;
        if (!switch_standing_) ask_switch_or_refuse_(pending_, pending_tag_);
        return;
    }
    recover_polls_cell_.publish(++recover_polls_);
    ++recover_tries_;
    if (readiness_poll_budget_ == 0 || recover_tries_ < readiness_poll_budget_) {
        return;
    }
    recovering_ = false;

    settle_reboot_(fabric_unconfigured_());
}

void SessionOwner::publish_fabric_() {
    FabricState st{};
    st.gen = fabric_gen_;
    st.cookie = cookie_;
    st.configured = programmer_.programmed();
    st.bridges_down = programmer_.bridges_down();
    st.load_ok = load_ok_;
    cell_.publish(st);
}

void SessionOwner::adopt_(const Link& link) {
    handoff_ = link.boot_handoff;
    launcher_cell_ = link.launcher_demand;
    launcher_wake_ = link.launcher_wake;
    publish_fabric_();
}

void SessionOwner::service_boot(const UiRequest::LoadCore& req) {

    publish_fabric_();
    note_memsz_(req);
    pending_ = req;
    pending_tag_ = kUncaused;

    front_end_fallback_armed_ =
        !req.path.empty() && !names_front_end_image(req.path.view(), req.xml);
    before_seats_ = true;
    start_refused_ = false;
    order_hold_reset_();
    order_bind_decoders_(proto::LinkOp::DecoderTable::PreSession, kBootGen);

    const StepArg arg = step_arg_(program_at_boot_());
    pending_step_arg_ = arg;
    (void)order_refusable_(
        proto::LinkOp::ApplyCore{.loaded = arg.loaded, .gen = as_generation_(kBootGen)});
    awaiting_conf_str_ = arg.loaded;
    if (facts_pending_) {
        order_bind_facts_();
        facts_pending_ = false;
    }

    publish_fabric_();
}

SessionOwner::Programmed SessionOwner::program_at_boot_() {
    if (pending_.path.empty()) return Programmed::NotNeeded;
    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    if (!rel) {
        ++programs_failed_;
        return Programmed::Failed;
    }
    ++programs_run_;
    const bool ok = programmer_.program_at_boot(*rel).has_value();
    if (!ok) ++programs_failed_;
    load_ok_ = ok;
    ++fabric_gen_;
    if (ok) latch_mra_facts_();
    return ok ? Programmed::Ok : Programmed::Failed;
}

void SessionOwner::refuse_file_ask_(UiRequest::Kind asked, CorrelationTag tag, Errc code) noexcept {

    publish_refusal_(asked, code, tag);
    ++files_failed_;
}

Ex<proto::ConfStrFileRow> SessionOwner::resolve_load_(const UiRequest::LoadFile& r) const {
    if (!conf_str_) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});

    if (!in_scope_(r.scope)) return std::unexpected(Error{Errc::stale, ERR_SITE(), r.scope.gen.v});
    const auto row = conf_str_->menu_pick(r.item, r.index);
    if (!row) return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.item.v});
    return *row;
}

Ex<proto::ConfStrFileRow> SessionOwner::resolve_load_(const UiRequest::LoadFileByDigit& r) {

    if (conf_str_ && !in_scope_(r.scope))
        return std::unexpected(Error{Errc::stale, ERR_SITE(), r.scope.gen.v});
    const proto::FileSlotHit hit =
        conf_str_ ? conf_str_->find_load_slot(r.digit) : proto::FileSlotHit{};
    if (!hit.entry) return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.digit.v});

    if (hit.how == proto::FileSlotMatch::RowZero) mgl_row0_cell_.publish(++mgl_row0_);
    return proto::ConfStr::row_of(*hit.entry);
}

template <class R>
void SessionOwner::load_file_(UiRequest::Kind asked, const R& req, CorrelationTag tag) {

    const cores::RamImageRecipe ram_image = std::exchange(pending_ram_image_, {});
    if (req.path.empty()) return refuse_file_ask_(asked, tag, Errc::bad_format);

    const auto row = resolve_load_(req);

    if (!core_live_) return refuse_file_ask_(asked, tag, Errc::core_load);
    if (!conf_str_ || boot_restore_owed_) return refuse_file_ask_(asked, tag, Errc::negotiation);
    if (!row) return refuse_file_ask_(asked, tag, row.error().code);
    auto cr = content_for_(*row, req.path.view());
    if (!cr) return refuse_file_ask_(asked, tag, Errc::bad_format);
    cr->asked = asked;
    cr->tag = tag;
    UiRequest::SaveChoice choice = UiRequest::SaveChoice::User;
    if constexpr (requires { req.save; }) choice = req.save;
    const PickedLoad load{.content = *cr, .opensave = row->opensave, .choice = choice, .tag = tag};
    plan_addons_(*row, req.path.view(), load, ram_image);
    if (!picked_load_) run_load_(load);
}

void SessionOwner::plan_addons_(const proto::ConfStrFileRow& row, std::string_view pick,
                                const PickedLoad& load, const cores::RamImageRecipe& image) {
    AddonSend addons = AddonSend::plan(row.addon, pick, load.content.slot);
    if (image.size() != 0 && !addons.take_image(cores::expand(image))) {
        ++ram_images_declined_;
        (void)owner_events_.push(
            infra::make<Event>(Event::RamImageDeclined{.why = Errc::negotiation},
                               Event::Head{EmitSite{ERR_SITE()}, {}, load.tag}));
    }
    if (addons.empty()) return;
    if (row.addon_after) {
        addons_after_.emplace(std::move(addons));
        return;
    }
    addons_.emplace(std::move(addons));
    picked_load_ = load;
}

void SessionOwner::run_load_(const PickedLoad& load) {
    if (const auto gen = attach_row_savestates_(load.opensave, load.content.path.view())) {
        ss_hold_ = SaveStateHold{.load = load,
                                 .gen = *gen,
                                 .due_ns = clock_.now().count() + cores::kSaveStateHoldBoundNs};
        return;
    }
    run_attached_load_(load);
}

void SessionOwner::run_attached_load_(const PickedLoad& load) {
    if (owns_loader_() && arm_loader_(load.content)) return;
    order_row_content_(load.content, load.opensave, load.choice, load.tag);
}

void SessionOwner::step_savestate_hold_() {
    if (!ss_hold_) return;
    if (!savestates_answered_(ss_hold_->gen)) {
        if (clock_.now().count() < ss_hold_->due_ns) return;
        ++savestate_hold_expiries_;
    }
    const PickedLoad load = ss_hold_->load;
    ss_hold_.reset();
    run_attached_load_(load);
}

bool SessionOwner::content_ordered_() const noexcept {
    const bool rom_waits = save_mount_ && save_mount_->then_load;
    return !picked_load_ && !ss_hold_ && !pieces_ && !load_ && !walk_ && !companion_walk_ &&
           !rom_waits;
}

void SessionOwner::step_addons_() {
    if (!addons_ && addons_after_ && content_ordered_()) {
        addons_ = std::move(addons_after_);
        addons_after_.reset();
    }
    if (!addons_) return;
    if (addons_->step(*this, vfs_) == AddonSend::Pass::Waiting) return;
    addons_sent_ += addons_->sent();
    addons_missing_ += addons_->missing();
    if (addons_->image_sent()) ++ram_images_sent_;
    if (addons_->image_failed()) {
        ++ram_images_declined_;
        (void)owner_events_.push(
            infra::make<Event>(Event::RamImageDeclined{.why = Errc::io},
                               Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
    }
    addons_.reset();
    if (picked_load_) {
        const PickedLoad load = *picked_load_;
        picked_load_.reset();
        run_load_(load);
    }
}

std::optional<ContentRequest> SessionOwner::content_for_(const proto::ConfStrFileRow& row,
                                                         std::string_view path) const {
    ContentRequest cr{};
    if (!cr.path.assign(path)) return std::nullopt;
    cr.slot = static_cast<std::uint8_t>(proto::ConfStr::wire_index(row, path));
    if (const auto member = zip_member_(row, path)) {
        cr.path = *member;
        cr.slot = static_cast<std::uint8_t>(proto::ConfStr::wire_index(row, member->view()));
    }
    cr.load_addr = row.load_addr;
    return cr;
}

std::optional<std::uint32_t> SessionOwner::attach_row_savestates_(bool opensave,
                                                                  std::string_view rom) {
    if (!opensave || !conf_str_ || !conf_str_->savestate().has_value()) return std::nullopt;
    const std::uint32_t gen = next_generation();
    if (!ask_savestates_(rom, gen)) return std::nullopt;
    return gen;
}

bool SessionOwner::ask_savestates_(std::string_view rom, std::uint32_t gen) noexcept {
    const bool service =
        save_state_asks_ && save_state_facts_ && save_state_facts_->generation() != 0;
    SaveStateAttach::Ask ask{.decl = *conf_str_->savestate(),
                             .core_name = core_name_,
                             .arcade = made_with_manifest_,
                             .generation = gen};
    if (!service || !ask.rom_path.assign(rom) || !save_state_asks_->push(ask)) {
        ++savestate_refusals_;
        return false;
    }
    return true;
}

bool SessionOwner::savestates_answered_(std::uint32_t gen) const noexcept {
    if (!save_state_facts_) return true;
    const std::uint32_t answered = save_state_facts_->sample().value.answered;
    return static_cast<std::int32_t>(answered - gen) >= 0;
}

bool SessionOwner::order_savestates(std::string_view image, std::uint32_t gen) {
    TASTY_SEAT_BODY(SessionOwner);
    if (!conf_str_ || !conf_str_->savestate().has_value()) return false;
    return ask_savestates_(image, gen);
}

std::uint32_t SessionOwner::savestates_answered() {
    TASTY_SEAT_BODY(SessionOwner);
    return save_state_facts_ ? save_state_facts_->sample().value.answered : 0u;
}

void SessionOwner::order_row_content_(const ContentRequest& cr, bool opensave,
                                      UiRequest::SaveChoice choice, CorrelationTag tag) {
    if (opensave && arm_open_save_(cr, choice, tag)) return;
    launch_content_(cr);
}

void SessionOwner::launch_content_(const ContentRequest& cr) {
    if (owns_loader_() || !arm_loader_(cr)) perform_content_(cr);
}

const cores::CoreFactory* SessionOwner::load_row_() const {
    const auto hint = made_with_manifest_ ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    const auto row = cores::find_core(core_name_.view(), hint);
    return row ? *row : nullptr;
}

bool SessionOwner::owns_loader_() const {
    const cores::CoreFactory* row = load_row_();
    return row != nullptr && row->make_loader != nullptr;
}

bool SessionOwner::arm_open_save_(const ContentRequest& cr, UiRequest::SaveChoice choice,
                                  CorrelationTag tag) {
    const auto core = cores::find_core(core_name_.view());
    if (core && (*core)->make_ladder != nullptr) return false;
    if (save_mount_ || held_mount_ || ladder_ != nullptr) {
        ++open_saves_skipped_;
        return false;
    }
    auto path = open_save_path_(cr.path.view(), choice);
    if (!path) {
        ++open_saves_skipped_;
        return false;
    }
    save_mount_.emplace(SaveMount{.ask = MountAsk{.which = UiRequest::Kind::LoadFile,
                                                  .index = proto::IoIndex{0},
                                                  .path = *path,
                                                  .tag = tag},
                                  .due_ns = clock_.now().count() + kSaveMountBoundNs,
                                  .then_load = cr});
    ++open_saves_armed_;
    return true;
}

std::optional<PathText> SessionOwner::open_save_path_(std::string_view rom,
                                                      UiRequest::SaveChoice choice) {
    if (core_name_.empty()) return std::nullopt;
    const bool replay = choice != UiRequest::SaveChoice::User;
    if (replay && replay_save_root_.empty()) return std::nullopt;
    std::string dir(replay ? replay_save_root_.view() : std::string_view{"saves"});
    dir += '/';
    dir.append(core_name_.view());
    if (!vfs_.ensure_dir(dir)) return std::nullopt;
    const std::size_t slash = rom.rfind('/');
    std::string_view name = slash == std::string_view::npos ? rom : rom.substr(slash + 1);
    if (const std::size_t dot = name.rfind('.'); dot != std::string_view::npos)
        name = name.substr(0, dot);
    std::string path = dir;
    path += '/';
    path.append(name);
    path += ".sav";
    if (replay && !prime_replay_save_(path, choice == UiRequest::SaveChoice::ReplaySeeded))
        return std::nullopt;
    PathText out{};
    if (!out.assign(path)) return std::nullopt;
    return out;
}

bool SessionOwner::prime_replay_save_(std::string_view path, bool seeded) {
    std::vector<std::uint8_t> seed;
    if (seeded) {
        std::string from(replay_save_root_.view());
        from += "/seed.sav";
        auto f = vfs_.open(from, svc::OpenMode::ReadWhole);
        const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
        if (!sz) return false;
        auto buf = read_whole_(**f, sz->v);
        if (!buf) return false;
        seed = std::move(*buf);
    }
    return durable_write(vfs_, path, std::as_bytes(std::span<const std::uint8_t>(seed)))
        .has_value();
}

std::optional<PathText> SessionOwner::bare_zip_member(std::string_view path, std::string_view exts,
                                                      std::span<const svc::DirEntry> listing) {
    if (!wants_zip_member(path, exts)) return std::nullopt;
    const auto file = std::ranges::find_if(listing, [exts](const svc::DirEntry& e) {
        return !e.is_dir && svc::Vfs::extension_matches(e.name, exts);
    });
    PathText out{};
    if (file == listing.end() || !out.assign(path) || !out.append("/") || !out.append(file->name)) {
        return std::nullopt;
    }
    return out;
}

std::optional<PathText> SessionOwner::zip_member_(const proto::ConfStrFileRow& row,
                                                  std::string_view path) const {
    if (!wants_zip_member(path, row.ext)) return std::nullopt;
    const auto members =
        vfs_.scan(path, svc::ScanFilter{.extensions = row.ext, .directories = false});
    if (!members) return std::nullopt;
    return bare_zip_member(path, row.ext, *members);
}

void SessionOwner::perform_content_(const ContentRequest& req) {
    content_owed_ = Owed{req.asked, req.tag};
    if (req.path.empty()) {
        ++files_failed_;
        return;
    }
    const std::string_view path = req.path.view();
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) {
        ++files_failed_;
        return;
    }

    const bool quiet = !req.whole && ladder_ == nullptr && !save_mount_ && !held_mount_;
    if (sz->v > InternedFile::kPieceBytes && quiet && streams_files_()) {
        auto p = FileTxPieces::start(std::move(*f), sz->v, req.slot, path);
        if (!p) {
            ++files_failed_;
            return;
        }
        forget_watch_();
        pieces_.emplace(std::move(*p));
        return;
    }
    auto buf = read_whole_(**f, sz->v);
    if (!buf) {
        ++files_failed_;
        return;
    }
    const std::size_t dot = path.rfind('.');
    const std::string_view ext =
        dot == std::string_view::npos ? std::string_view{} : path.substr(dot);
    LoadKey key(buf->size());
    key.feed(*buf);
    const auto id = inbox_.intern_file(std::move(*buf), ext, path, req.load_addr, key.value());
    if (!id) {
        ++files_failed_;
        return;
    }
    if (req.save != proto::FileId{} && !inbox_.stamp_save(*id, req.save)) ++open_saves_skipped_;
    if (req.edge != ContentEdge::Content) (void)inbox_.stamp_edge(*id, req.edge);

    ContentWatch watch{.file = std::move(*f),
                       .total = sz->v,
                       .crc = key.value(),
                       .wire_index = req.slot,
                       .cut_on_hold =
                           req.save != proto::FileId{} || req.edge != ContentEdge::Content};
    if (!watch.path.assign(path)) watch.file.reset();
    if (!order_content_tx_(*id, std::move(watch))) {
        ++op_drops_;
        return;
    }
    ++ops_posted_;
    ++files_loaded_;
    publish_done_(req.asked, req.tag);
    if (req.load_addr == 0) begin_companion_(path);
}

bool SessionOwner::order_content_tx_(proto::FileId file, ContentWatch watch) {
    watch.act = next_generation();
    const proto::LinkOp::FileTx op{.wire_index = watch.wire_index, .file = file, .act = watch.act};
    if (!inbox_.push(op)) return false;
    watch_.emplace(std::move(watch));
    return true;
}

bool SessionOwner::watch_answered_() const noexcept {
    if (!watch_) return false;
    const auto s = ftx_level_cell_.sample();
    return s && s.value.answers(watch_->act);
}

void SessionOwner::forget_watch_() noexcept {
    if (!watch_) return;
    watch_.reset();
    ++long_wait_drops_;
}

void SessionOwner::step_watch_() {
    if (!watch_) return;
    const auto s = ftx_level_cell_.sample();
    if (!s || !s.value.answers(watch_->act)) return;
    const FileTxLevel& l = s.value;
    if (l.held_act != watch_->act) {
        watch_.reset();
        return;
    }

    if (pieces_ || load_ || walk_ || companion_walk_) {
        forget_watch_();
        return;
    }
    ContentWatch& w = *watch_;
    if (!w.cut_on_hold && w.file) {
        auto p = FileTxPieces::resume(std::move(w.file), w.total, w.wire_index, w.path.view(),
                                      w.from + l.held_bytes, w.crc);
        if (p) {
            pieces_.emplace(std::move(*p));
            ++long_wait_resumes_;
            watch_.reset();
            return;
        }
    }
    const proto::LinkOp::FileTx cut{.wire_index = w.wire_index,
                                    .phase = proto::LinkOp::FileTxPhase::Close,
                                    .total = static_cast<std::uint32_t>(w.total)};
    if (!inbox_.push(cut)) return;
    ++ops_posted_;
    ++files_failed_;
    ++long_wait_refusals_;
    watch_.reset();
}

void SessionOwner::watch_last_piece_() {
    const FileTxPieces::LastOp l = pieces_->last_op();
    ContentWatch w{.act = l.act,
                   .file = pieces_->take_file(),
                   .total = pieces_->total(),
                   .crc = l.crc,
                   .wire_index = pieces_->wire_index(),
                   .from = l.from,
                   .cut_on_hold = l.shaped};
    if (!w.path.assign(pieces_->path())) w.file.reset();
    watch_.emplace(std::move(w));
}

void SessionOwner::step_pieces_() {
    if (!pieces_ || !pieces_->ready(inbox_, ftx_level_cell_)) return;
    const std::uint32_t before = inbox_.ring().pushed();
    const FileTxPieces::Pass pass = pieces_->step(inbox_, ftx_level_cell_, next_generation());
    ops_posted_ += static_cast<std::uint32_t>(inbox_.ring().pushed() - before);
    if (pass == FileTxPieces::Pass::Done) {
        if (!pieces_->resumed()) {
            ++files_loaded_;
            publish_done_(content_owed_.which, content_owed_.tag);
        }
        watch_last_piece_();
    } else if (pass == FileTxPieces::Pass::Failed) {
        ++files_failed_;
    } else {
        return;
    }
    pieces_.reset();
}

bool SessionOwner::rung_pending() const noexcept { return rung_wait_() == RungWait::Now; }

SessionOwner::RungWait SessionOwner::rung_wait_() const noexcept {

    if (reboot_owed_) return RungWait::None;
    const auto each = [](bool live, bool now) noexcept {
        return !live ? RungWait::None : (now ? RungWait::Now : RungWait::Later);
    };
    const bool room = inbox_.ring().size() < proto::kLinkTxCapacity;
    return std::max({
        each(pieces_.has_value(), pieces_ && pieces_->ready(inbox_, ftx_level_cell_)),
        each(watch_.has_value(), watch_answered_() && room),
        each(load_.has_value(), load_ && load_->wants_pass(inbox_, &ftx_level_cell_)),
        each(walk_.has_value(), walk_ && walk_->wants_pass(inbox_, &ftx_level_cell_)),
        each(companion_walk_.has_value(),
             companion_walk_ && companion_walk_->wants_pass(inbox_, &ftx_level_cell_)),
        each(ladder_ != nullptr, (ladder_moved_ || walk_answer_fresh_) && room),
        each(save_mount_.has_value(), false),
        each(ss_hold_.has_value(), ss_hold_ && savestates_answered_(ss_hold_->gen)),
        each(addons_.has_value(), addons_ && addons_->wants_pass(inbox_)),
        each(!addons_ && addons_after_.has_value(), content_ordered_()),
        each(manifest_.has_value(), manifest_ && manifest_->wants_pass(inbox_, &ftx_level_cell_)),
    });
}

bool SessionOwner::arm_loader_(const ContentRequest& req, std::string_view save,
                               std::uint64_t save_bytes) {
    const cores::CoreFactory* row = load_row_();
    const cores::MakeLoader make = cores::loader_for(row, req.load_addr);
    if (make == nullptr) return false;
    const bool own = row != nullptr && row->make_loader != nullptr;

    if (!own && (load_ || walk_ || pieces_)) {
        ++files_failed_;
        return true;
    }
    content_owed_ = Owed{req.asked, req.tag};
    if (own && cores::walks_romset(row->profile->slots, proto::IoIndex{req.slot})) {
        return arm_walk_(req, *row);
    }
    const cores::CoreProfile& profile =
        row != nullptr ? *row->profile : cores::profile_for(core_name_.view());
    const std::string_view path = req.path.view();
    auto f = vfs_.open(path, svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) {
        ++files_failed_;
        return true;
    }
    const bool turbo = conf_str_ && conf_str_->declares_turbo();
    auto loader = make(cores::LoaderContext{vfs_, profile, turbo, loader_memo_, aperture_});
    const cores::LoadAsk ask{
        .index = proto::IoIndex{req.slot}, .path = path, .size = sz->v, .load_addr = req.load_addr};
    const auto plan = loader->plan(ask, status_word());
    if (!plan || plan->count == 0) {
        ++files_failed_;
        return true;
    }

    if (plan->count > 1) {
        auto w = LoadWalk::start(std::move(loader), *plan);
        if (!w) {
            ++files_failed_;
            return true;
        }
        walk_windows_ = profile.windows;
        forget_watch_();
        walk_.emplace(std::move(*w));
        return true;
    }
    const cores::TransferRow& r = plan->rows[0];
    const bool plain = !r.shaped && !r.placed;
    if (plain && r.dest != cores::RowDest::Window) return false;
    if (r.dest == cores::RowDest::Window) {
        const std::uint64_t extent = std::max<std::uint64_t>(sz->v, r.extent);
        auto w = LoadWindow<seat_owner::Main>::open(window_map_, aperture_, os::PhysAddr{r.addr},
                                                    extent);
        std::optional<LoadWindow<seat_owner::Main>> mirror{};
        if (w && r.mirror != 0) {
            auto m = LoadWindow<seat_owner::Main>::open(window_map_, aperture_,
                                                        os::PhysAddr{r.mirror}, extent);
            if (m) {
                mirror.emplace(std::move(*m));
            } else {
                w = std::unexpected(m.error());
            }
        }
        if (w) {
            LoadLadder::Job job{.loader = std::move(loader),
                                .file = std::move(*f),
                                .total = sz->v,
                                .wire_index = req.slot,
                                .act = next_generation(),
                                .row = r,
                                .mirror = std::move(mirror),
                                .save_bytes = save_bytes};
            if (!job.save.assign(save)) {
                ++files_failed_;
                return true;
            }
            auto l = LoadLadder::start(std::move(job), std::move(*w), path);
            if (!l) {
                ++files_failed_;
                return true;
            }
            forget_watch_();
            load_.emplace(std::move(*l));
            return true;
        }
        ++window_counts_.refusals;
        window_counts_.refusal_code = static_cast<std::uint16_t>(w.error().code);
        publish_window_counts_();

        if (r.placed || r.mirror != 0) {
            ++files_failed_;
            return true;
        }

        if (plain) return false;
    }
    auto p = FileTxPieces::start(std::move(*f), sz->v, req.slot, path, std::move(loader), r);
    if (!p) {
        ++files_failed_;
        return true;
    }
    forget_watch_();
    pieces_.emplace(std::move(*p));
    return true;
}

bool SessionOwner::arm_walk_(const ContentRequest& req, const cores::CoreFactory& row) {
    const bool turbo = conf_str_ && conf_str_->declares_turbo();
    auto loader =
        row.make_loader(cores::LoaderContext{vfs_, *row.profile, turbo, loader_memo_, aperture_});
    const cores::LoadAsk ask{
        .index = proto::IoIndex{req.slot}, .path = req.path.view(), .load_addr = req.load_addr};
    const auto plan = loader->plan(ask, status_word());
    auto w = plan ? LoadWalk::start(std::move(loader), *plan)
                  : Ex<LoadWalk>{std::unexpected(plan.error())};
    if (!w) {
        ++files_failed_;
        return true;
    }
    walk_windows_ = row.profile->windows;
    forget_watch_();
    walk_.emplace(std::move(*w));
    return true;
}

bool SessionOwner::order_walk(proto::IoIndex slot, std::string_view path, std::uint32_t gen) {
    TASTY_SEAT_BODY(SessionOwner);
    if (walk_ || pieces_ || load_ || addons_) return false;
    content_owed_ = Owed{};
    walk_answer_ = cores::MountStatus{.generation = gen, .state = cores::MountState::Failed};
    const auto row = cores::find_core(core_name_.view());
    if (!row || (*row)->make_loader == nullptr) return true;
    const bool turbo = conf_str_ && conf_str_->declares_turbo();
    auto loader = (*row)->make_loader(
        cores::LoaderContext{vfs_, *(*row)->profile, turbo, loader_memo_, aperture_});
    const cores::LoadAsk ask{.index = slot, .path = path, .disc = true};
    const auto plan = loader->plan(ask, status_word());
    auto w = plan ? LoadWalk::start(std::move(loader), *plan)
                  : Ex<LoadWalk>{std::unexpected(plan.error())};
    if (!w) {
        ++files_failed_;
        return true;
    }
    walk_windows_ = (*row)->profile->windows;
    forget_watch_();
    walk_.emplace(std::move(*w));
    walk_gen_ = gen;
    walk_answer_.state = cores::MountState::Pending;
    return true;
}

void SessionOwner::step_walk_() {
    if (!walk_) return;
    LoadWalk::Host host{.rung = LoadLadder::Host{inbox_, &ftx_level_cell_, owner_events_},
                        .owner = *this,
                        .map = &window_map_,
                        .aperture = aperture_,
                        .vfs = vfs_,
                        .windows = walk_windows_,
                        .now_ns = clock_.now().count()};
    const std::uint32_t before = inbox_.ring().pushed();
    const LoadWalk::Pass pass = walk_->step(host);
    ops_posted_ += static_cast<std::uint32_t>(inbox_.ring().pushed() - before);
    if (pass == LoadWalk::Pass::Done) {
        ++files_loaded_;
        publish_done_(content_owed_.which, content_owed_.tag);
    } else if (pass == LoadWalk::Pass::Failed) {
        ++files_failed_;
        if (walk_->refused() || walk_->stalled()) {
            (void)owner_events_.push(
                infra::make<Event>(Event::InfoRequest{.id = InfoId::RomLoadFailed},
                                   Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
        }
    } else {
        return;
    }
    if (walk_gen_ != 0) {
        walk_answer_fresh_ = true;
        walk_answer_ =
            cores::MountStatus{.generation = walk_gen_,
                               .state = pass == LoadWalk::Pass::Done ? cores::MountState::Done
                                                                     : cores::MountState::Failed};
        walk_gen_ = 0;
    }
    walk_.reset();
}

void SessionOwner::make_companion_() {
    withdraw_companion_();
    const auto hint = made_with_manifest_ ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    const auto row = cores::find_core(core_name_.view(), hint);
    if (!row || (*row)->make_companion == nullptr) return;
    companion_ = (*row)->make_companion(vfs_);
    if (!companion_binds_ || companion_ == nullptr) return;
    companion_attached_ = companion_binds_->push(
        infra::make<CompanionBind>(CompanionBind::Attach{.servant = companion_->servant()}));
    if (!companion_attached_) ++companion_attach_refusals_;
}

void SessionOwner::withdraw_companion_() noexcept {
    companion_walk_.reset();
    if (companion_ == nullptr) return;
    if (companion_attached_ && !companion_binds_->push(infra::make<CompanionBind>(
                                   CompanionBind::Attach{.servant = cores::ServantId::None})))
        ++companion_attach_refusals_;
    companion_attached_ = false;
    companion_.reset();
}

void SessionOwner::begin_companion_(std::string_view path) {
    if (companion_ == nullptr) return;
    if (companion_walk_) {
        ++companion_skips_;
        return;
    }
    const cores::CompanionAsk ask{
        .path = path, .mailbox_live = companion_attached_, .aperture_base = aperture_.phys.v};
    auto plan = companion_->plan(ask);
    if (!plan) {
        ++companion_skips_;
        return;
    }
    companion_gen_ = static_cast<std::uint16_t>(companion_gen_ + 1u);
    if (companion_gen_ == 0) companion_gen_ = 1;
    auto w =
        CompanionWalk::start(*plan, companion_gen_, plan->loads ? companion_->loader() : nullptr);
    if (!w) {
        ++companion_skips_;
        return;
    }
    forget_watch_();
    companion_walk_.emplace(std::move(*w));
    ++companion_walks_;

    if (!companion_walk_->loads()) step_companion_();
    if (companion_walk_ && !companion_walk_->loads()) step_companion_();
}

void SessionOwner::step_companion_() {
    if (!companion_walk_) return;
    CompanionWalk::Host host{
        .load = LoadWalk::Host{.rung = LoadLadder::Host{inbox_, &ftx_level_cell_, owner_events_},
                               .owner = *this,
                               .map = &window_map_,
                               .aperture = aperture_,
                               .vfs = vfs_,
                               .windows = loaded_profile_().windows,
                               .now_ns = clock_.now().count()},
        .binds = companion_attached_ ? companion_binds_ : infra::OptRef<CompanionHost::Binds>{}};
    const std::uint32_t before = inbox_.ring().pushed();
    const CompanionWalk::Pass pass = companion_walk_->step(host);
    ops_posted_ += static_cast<std::uint32_t>(inbox_.ring().pushed() - before);
    if (pass == CompanionWalk::Pass::Done) companion_walk_.reset();
}

void SessionOwner::step_load_() {
    if (!load_) return;
    LoadLadder::Host host{inbox_, &ftx_level_cell_, owner_events_};
    const std::uint32_t before = inbox_.ring().pushed();
    const LoadLadder::Pass pass = load_->step(host);
    ops_posted_ += static_cast<std::uint32_t>(inbox_.ring().pushed() - before);
    if (pass != LoadLadder::Pass::Done && pass != LoadLadder::Pass::Failed) return;
    load_.reset();
    if (pass == LoadLadder::Pass::Done) {
        ++files_loaded_;
        ++window_counts_.windows;
        publish_done_(content_owed_.which, content_owed_.tag);
    } else {
        ++files_failed_;
    }
    publish_window_counts_();
}

void SessionOwner::publish_window_counts_() noexcept { window_cell_.publish(window_counts_); }

const cores::CoreProfile& SessionOwner::loaded_profile_() const {
    const auto hint = made_with_manifest_ ? cores::LoadHint::XmlManifest : cores::LoadHint::None;
    const auto row = cores::find_core(core_name_.view(), hint);
    return row ? *(*row)->profile : cores::profile_for({});
}

bool SessionOwner::streams_files_() const { return !loaded_profile_().file_tx_whole; }

std::uint32_t SessionOwner::next_generation() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++act_gen_;
    if (act_gen_ == 0) act_gen_ = 1;
    return act_gen_;
}

bool SessionOwner::order(const proto::LinkOp& op) {
    TASTY_SEAT_BODY(SessionOwner);
    if (!inbox_.push(op)) {
        ++op_drops_;
        return false;
    }
    ++ops_posted_;
    return true;
}

std::optional<std::vector<std::uint8_t>> SessionOwner::read_whole_(svc::IFile& f,
                                                                   std::uint64_t size) {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = f.read_at(off, std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off)));
        if (!n || *n == 0) return std::nullopt;
        off += *n;
    }
    return buf;
}

Ex<proto::FileId> SessionOwner::intern_bytes(std::span<const std::uint8_t> bytes,
                                             std::string_view ext, std::uint32_t load_addr) {
    TASTY_SEAT_BODY(SessionOwner);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    auto id = inbox_.intern_file(std::move(buf), ext, std::string_view{}, load_addr);
    if (!id) return std::unexpected(id.error());
    return *id;
}

Ex<proto::FileId> SessionOwner::intern_piece(std::span<const std::uint8_t> bytes,
                                             std::string_view ext, std::uint64_t whole,
                                             std::uint64_t offset) {
    TASTY_SEAT_BODY(SessionOwner);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    return inbox_.intern_file(std::move(buf), ext, std::string_view{}, 0, 0, whole, offset);
}

Ex<proto::FileId> SessionOwner::intern_path(std::string_view path, std::uint64_t size_bytes) {
    TASTY_SEAT_BODY(SessionOwner);
    auto id = inbox_.intern_file_path(path, size_bytes);
    if (!id) return std::unexpected(id.error());
    ++images_sized_;
    return *id;
}

cores::MountStatus SessionOwner::mount_status() {
    TASTY_SEAT_BODY(SessionOwner);
    return mount_status_cell_.sample().value;
}

cores::SaveExtent SessionOwner::save_extent() {
    TASTY_SEAT_BODY(SessionOwner);
    return save_extent_cell_.sample().value;
}

proto::StatusWord SessionOwner::status_word() {
    TASTY_SEAT_BODY(SessionOwner);
    return status_cell_.sample().value;
}

void SessionOwner::bios_missing() {
    TASTY_SEAT_BODY(SessionOwner);

    (void)owner_events_.push(infra::make<Event>(Event::InfoRequest{.id = InfoId::CdBiosMissing},
                                                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
}

void SessionOwner::on_mount_image_(const MountAsk& ask) {
    if (mount_busy_()) {
        if (held_mount_.has_value() && (start_ladder_ || start_mounts_live_())) {

            publish_refusal_(ask.which, Errc::would_block, ask.tag);
            return;
        }
        if (held_mount_.has_value()) {
            ++mounts_refused_;
            publish_refusal_(ask.which, Errc::negotiation, ask.tag);
            return;
        }
        held_mount_ = ask;
        ++mounts_held_;
        return;
    }
    arm_mount_(ask);
}

void SessionOwner::arm_mount_(const MountAsk& ask) {
    if (core_name_.empty()) {
        ++mounts_refused_;
        publish_refusal_(ask.which, Errc::core_load, ask.tag);
        return;
    }
    ladder_slot_ = ask.index.v;
    const cores::CoreProfile& p = loaded_profile_();
    switch (cores::role_of(p, ask.index)) {
        case cores::SlotRole::Disc: {
            const auto row = cores::find_core(core_name_.view());
            if (!row) break;
            arm_ladder_(ask, **row);
            return;
        }
        case cores::SlotRole::Save: {
            ++mounts_armed_;
            const cores::StagingPolicy& sp = p.staging;
            const bool framed = sp.save_dest != proto::WideIoIndex{} && sp.save_slot == ask.index;
            MountAsk routed = ask;
            if (!framed && !routed.index_word)
                routed.index_word = pick_index_word_(ask.index, ask.path.view());
            save_mount_.emplace(SaveMount{.ask = routed,
                                          .due_ns = clock_.now().count() + kSaveMountBoundNs,
                                          .bracketed = framed});
            return;
        }
        case cores::SlotRole::Image:
            break;
    }
    arm_plain_mount_(ask);
}

std::optional<std::uint8_t> SessionOwner::pick_index_word_(proto::IoIndex index,
                                                           std::string_view path) const {
    if (!conf_str_) return std::nullopt;
    const proto::ConfStrEntry* row = nullptr;
    for (const proto::ConfStrEntry& e : conf_str_->file_slots())
        if (e.letter == 'S' && e.ioctl_index == index.v) row = &e;
    if (row == nullptr) return std::nullopt;
    return static_cast<std::uint8_t>((proto::ConfStr::ext_subindex(path, row->ext) << 6) |
                                     (row->selentry + 1));
}

void SessionOwner::arm_ladder_(const MountAsk& ask, const cores::CoreFactory& row) {
    const cores::LadderContext ctx{.vfs = vfs_,
                                   .host = *this,
                                   .clock = clock_,
                                   .image_path = ask.path.view(),
                                   .last_dir = ladder_last_dir_,
                                   .noreset = ladder_noreset_};
    ladder_ = row.make_ladder != nullptr ? row.make_ladder(*row.profile, ctx) : nullptr;
    if (ladder_ == nullptr) {
        ++mounts_refused_;
        publish_refusal_(ask.which, Errc::core_load, ask.tag);
        return;
    }

    ladder_last_dir_.assign(ladder_->next_last_dir());
    ladder_moved_ = true;
    ladder_owed_ = Owed{ask.which, ask.tag};
    ++mounts_armed_;
    ++ladders_run_;
    publish_ladder_(true);
}

void SessionOwner::arm_plain_mount_(const MountAsk& ask) {
    const std::string_view path = ask.path.view();
    proto::LinkOp::BindMount op{.index = ask.index, .perform = true};
    if (!path.empty()) {
        std::uint64_t size = 0;
        if (auto f = vfs_.open(path, svc::OpenMode::ReadWrite)) {
            if (auto sz = (*f)->size()) size = sz->v;
        }
        const auto id = inbox_.intern_file_path(path, size);
        if (!id || *id == proto::FileId{}) {

            ++files_failed_;
            ++mounts_refused_;
            (void)owner_events_.push(
                infra::make<Event>(Event::InfoRequest{.id = InfoId::ImageMountFailed},
                                   Event::Head{EmitSite{ERR_SITE()}, {}, ask.tag}));
            return;
        }
        op.image = *id;
        ++images_sized_;
    }
    ++mounts_armed_;
    if (order(op)) publish_done_(ask.which, ask.tag);
}

void SessionOwner::step_save_mount_() {
    if (!save_mount_) return;
    SaveMount& m = *save_mount_;
    const proto::SlotIndex slot{m.ask.index.v};
    const std::string_view path = m.ask.path.view();

    if (m.gen != 0) {
        const cores::SaveExtent e = save_extent();
        if (e.generation == m.gen && e.answer == cores::SaveAnswer::Declined) m.gen = 0;
        if (e.generation == m.gen && e.answer == cores::SaveAnswer::Known) {
            if (path.empty()) {
                finish_save_mount_(true);
                return;
            }
            if (m.then_load) {

                if (!owns_loader_() && arm_loader_(*m.then_load, path, e.size_bytes)) {
                    finish_save_mount_(true, true);
                    return;
                }

                if (const auto id = intern_path(path, e.size_bytes); id && *id != proto::FileId{}) {
                    ContentRequest load = *m.then_load;
                    load.save = *id;
                    perform_content_(load);
                    finish_save_mount_(true, true);
                    return;
                }
            } else {
                const auto id = intern_path(path, e.size_bytes);
                if (id && *id != proto::FileId{} &&
                    order(proto::LinkOp::BindSlot{.slot = slot,
                                                  .bind = proto::LinkOp::SlotBind::Mount,
                                                  .bracketed = m.bracketed,
                                                  .path = *id,
                                                  .sends_index = m.ask.index_word.has_value(),
                                                  .index_word = m.ask.index_word.value_or(0)})) {
                    finish_save_mount_(true);
                    return;
                }
            }
        }
    }
    if (clock_.now().count() >= m.due_ns) {

        if (!path.empty() && m.gen != 0)
            (void)order(proto::LinkOp::BindSlot{.slot = slot,
                                                .bind = proto::LinkOp::SlotBind::Detach,
                                                .act_gen = next_generation()});
        if (m.then_load) {

            const ContentRequest load = *m.then_load;
            ++open_saves_skipped_;
            launch_content_(load);
            finish_save_mount_(false, true);
            return;
        }
        finish_save_mount_(false);
        return;
    }
    if (m.gen != 0) return;
    if (path.empty()) {

        if (!m.unmounted) {
            if (!order(proto::LinkOp::BindSlot{.slot = slot,
                                               .bind = proto::LinkOp::SlotBind::Unmount,
                                               .bracketed = m.bracketed,
                                               .sends_index = m.ask.index_word.has_value(),
                                               .index_word = m.ask.index_word.value_or(0)}))
                return;
            m.unmounted = true;
        }
        const std::uint32_t g = next_generation();
        if (!order(proto::LinkOp::BindSlot{
                .slot = slot, .bind = proto::LinkOp::SlotBind::Detach, .act_gen = g}))
            return;
        m.gen = g;
        return;
    }
    const auto id = intern_path(path, 0);
    if (!id || *id == proto::FileId{}) return;
    const std::uint32_t g = next_generation();

    if (!order(proto::LinkOp::BindSlot{.slot = slot,
                                       .bind = proto::LinkOp::SlotBind::Attach,
                                       .manual = !m.bracketed && !m.then_load,
                                       .path = *id,
                                       .act_gen = g}))
        return;
    m.gen = g;
}

void SessionOwner::finish_save_mount_(bool ok, bool quiet) {
    if (!save_mount_) return;
    const std::uint8_t slot = save_mount_->ask.index.v;
    const CorrelationTag tag = save_mount_->ask.tag;
    const UiRequest::Kind which = save_mount_->ask.which;
    quiet = quiet || save_mount_->start;
    save_mount_.reset();
    if (ok && !quiet) {
        ++save_mounts_done_;
        (void)owner_events_.push(infra::make<Event>(
            Event::SdActivity{.slot = slot}, Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
        publish_done_(which, tag);
    } else if (!quiet) {
        (void)owner_events_.push(
            infra::make<Event>(Event::InfoRequest{.id = InfoId::ImageMountFailed},
                               Event::Head{EmitSite{ERR_SITE()}, {}, tag}));
    }
    arm_next_mount_();
}

void SessionOwner::step_ladder_() {
    walk_answer_fresh_ = false;
    if (ladder_ == nullptr) return;
    const std::uint32_t before = inbox_.ring().pushed();
    const std::uint32_t loaded = ladder_->report().assets_loaded;
    auto r = ladder_->step();
    ladder_moved_ = inbox_.ring().pushed() != before;
    files_loaded_ += ladder_->report().assets_loaded - loaded;
    if (!r) {
        finish_ladder_(false);
        return;
    }
    if (*r) {
        finish_ladder_(true);
        return;
    }
    publish_ladder_(true);
}

void SessionOwner::finish_ladder_(bool ok) {
    if (ladder_ == nullptr) return;
    if (!ok) ++ladder_failures_;
    publish_ladder_(false);

    ladder_last_dir_.assign(ladder_->next_last_dir());
    ladder_noreset_ = ladder_->next_noreset();
    const cores::BootLadder::Report rep = ladder_->report();
    if (rep.savestate_hold_expired) ++savestate_hold_expiries_;

    const bool touched = !start_ladder_ || rep.save_mounted || rep.save_detached;
    ladder_.reset();
    ladder_moved_ = false;
    start_ladder_ = false;
    start_pick_ladder_ = false;
    const Event::Head head{EmitSite{ERR_SITE()}, {}, kUncaused};
    const Owed owed = std::exchange(ladder_owed_, Owed{});
    if (ok) {
        if (touched)
            (void)owner_events_.push(
                infra::make<Event>(Event::SdActivity{.slot = ladder_slot_}, head));
        publish_done_(owed.which, owed.tag);
    } else {
        (void)owner_events_.push(
            infra::make<Event>(Event::InfoRequest{.id = InfoId::ImageMountFailed}, head));
    }

    if (reset_owed_.has_value()) {
        const proto::LinkOp::CoreReset op = *reset_owed_;
        reset_owed_.reset();
        (void)order_reset_(op, kUncaused);
    }
    arm_next_mount_();
}

void SessionOwner::arm_next_mount_() {
    while (!mount_busy_() && (!start_mounts_.empty() || !boot_images_.empty())) {
        if (!start_mounts_.empty()) {
            const MountAsk next = start_mounts_.front();
            start_mounts_.pop_front();
            arm_start_mount_(next);
            continue;
        }
        const MountAsk next = boot_images_.front();
        boot_images_.pop_front();
        arm_boot_image_(next);
    }
    if (mount_busy_() || !held_mount_.has_value()) return;
    const MountAsk next = *held_mount_;
    held_mount_.reset();
    arm_mount_(next);
}

void SessionOwner::arm_start_mount_(const MountAsk& ask) {
    const cores::CoreProfile& profile = loaded_profile_();
    switch (cores::start_mount_of(profile, ask.index)) {
        case cores::StartMount::AsPick:
            ++remembered_mounts_armed_;
            arm_mount_(ask);
            start_pick_ladder_ = ladder_ != nullptr;
            return;
        case cores::StartMount::Unmodelled:
            ++remembered_mounts_skipped_;
            return;
        case cores::StartMount::Generic:
            break;
    }
    switch (cores::role_of(profile, ask.index)) {
        case cores::SlotRole::Disc:
            ++remembered_mounts_skipped_;
            return;
        case cores::SlotRole::Save:

            ++remembered_mounts_armed_;
            ++mounts_armed_;
            ladder_slot_ = ask.index.v;
            save_mount_.emplace(SaveMount{.ask = ask,
                                          .due_ns = clock_.now().count() + kSaveMountBoundNs,
                                          .bracketed = false,
                                          .start = true});
            return;
        case cores::SlotRole::Image:
            break;
    }
    if (!vfs_.open(ask.path.view(), svc::OpenMode::Read)) {
        ++remembered_mounts_skipped_;
        return;
    }
    ++remembered_mounts_armed_;
    arm_plain_mount_(ask);
}

bool SessionOwner::read_remembered_mounts_() {
    start_mounts_.clear();
    if (!conf_str_ || remembered_files_ == RememberedFiles::Ignore) return false;
    bool slot0_held = false;
    const proto::ItemTable items = conf_str_->item_table();
    for (const proto::Item& it : items.items()) {
        if (it.kind != proto::ItemKind::MountSlot || !it.store_name || !it.has_digit) continue;
        const std::string path =
            read_remembered_path(vfs_, remembered_stem_, RememberedSlot::Mount, it.digit);
        if (path.empty()) continue;
        slot0_held = slot0_held || it.digit == 0;
        MountAsk ask{.which = UiRequest::Kind::MountImage,
                     .index = proto::IoIndex{it.digit},
                     .tag = kUncaused};
        ask.index_word = static_cast<std::uint8_t>(
            (proto::ConfStr::ext_subindex(path, items.subfield(it, 1)) << 6) | it.digit);
        if (!ask.path.assign(path)) {
            ++remembered_mounts_skipped_;
            continue;
        }
        start_mounts_.push_back(ask);
    }
    return slot0_held;
}

void SessionOwner::queue_boot_images_(const StartChain& chain) {
    boot_images_.clear();
    for (const StartFile& f : chain.boot_images()) {
        MountAsk ask{.which = UiRequest::Kind::MountImage,
                     .index = proto::IoIndex{static_cast<std::uint8_t>(f.dest.v >> 6)},
                     .path = f.path,
                     .tag = kUncaused,
                     .index_word = static_cast<std::uint8_t>(f.dest.v)};
        boot_images_.push_back(ask);
    }
}

void SessionOwner::arm_boot_image_(const MountAsk& ask) {
    switch (cores::role_of(loaded_profile_(), ask.index)) {
        case cores::SlotRole::Save:
            ++boot_images_armed_;
            ++mounts_armed_;
            ladder_slot_ = ask.index.v;
            save_mount_.emplace(SaveMount{.ask = ask,
                                          .due_ns = clock_.now().count() + kSaveMountBoundNs,
                                          .bracketed = false,
                                          .start = true});
            return;
        case cores::SlotRole::Disc:
        case cores::SlotRole::Image:
            ++boot_images_skipped_;
            return;
    }
}

void SessionOwner::publish_ladder_(bool live) {
    LadderCell rec{};
    if (ladder_ != nullptr) {
        rec.report = ladder_->report();
        rec.pc = ladder_->pc();
    }
    rec.live = live;
    ladder_cell_.publish(rec);
}

void SessionOwner::drop_ladder_() {
    if (ladder_ != nullptr) {
        publish_ladder_(false);
        ladder_.reset();
    }
    ladder_moved_ = false;
    start_ladder_ = false;
    start_pick_ladder_ = false;
    save_mount_.reset();
    held_mount_.reset();
    start_mounts_.clear();
    boot_images_.clear();
    pieces_.reset();
    watch_.reset();
    addons_.reset();
    picked_load_.reset();
    ss_hold_.reset();
    addons_after_.reset();
    manifest_.reset();
    if (load_) {
        load_.reset();
        ++window_counts_.cancelled;
        publish_window_counts_();
        (void)owner_events_.push(infra::make<Event>(
            Event::ProgressUpdate{}, Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
    }
    walk_gen_ = 0;
    if (walk_) {
        walk_.reset();
        ++window_counts_.cancelled;
        publish_window_counts_();
    }
    reset_owed_.reset();
    ladder_last_dir_.clear();
    ladder_noreset_ = false;
    core_name_.clear();
}

void SessionOwner::forget_core_() noexcept {
    withdraw_companion_();

    conf_str_.reset();
    pending_ram_image_ = {};
    core_live_ = false;
}

bool SessionOwner::intern_rel_at_(proto::FileId id, std::string_view rel) {
    std::vector<std::uint8_t> buf;
    if (!rel.empty()) {
        auto f = vfs_.open(rel, svc::OpenMode::ReadWhole);
        if (f) {
            auto sz = (*f)->size();
            if (sz && sz->v != 0) {
                buf.resize(static_cast<std::size_t>(sz->v));
                std::size_t off = 0;
                bool ok = true;
                while (off < buf.size()) {
                    auto n = (*f)->read_at(
                        off, std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off)));
                    if (!n || *n == 0) {
                        ok = false;
                        break;
                    }
                    off += *n;
                }
                if (!ok) buf.clear();
            }
        }
    }
    const bool has_bytes = !buf.empty();
    return inbox_.intern_file_at(id, std::move(buf), ".CFG", rel, 0).has_value() && has_bytes;
}

void SessionOwner::order_slot_previews_(const std::byte* pack, std::size_t len) {
    if (pack == nullptr || len == 0) return;
    const auto count = std::to_integer<std::uint8_t>(pack[0]);
    std::size_t off = 1;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (off >= len) break;
        const auto nlen = std::to_integer<std::uint8_t>(pack[off++]);
        if (off + nlen > len) break;
        const std::string_view name = infra::as_text(std::span(pack + off, nlen));
        off += nlen;
        std::string rel = "config/";
        rel.append(name);
        (void)intern_rel_at_(InternedFile::config_id(i), rel);
    }
    if (!inbox_.push(proto::LinkOp::BindSlotPreviews{.count = count})) {
        ++op_drops_;
        return;
    }
    ++ops_posted_;
}

void SessionOwner::intern_manifest_(std::string_view rel) {
    manifest_doc_ = read_manifest_(rel);
    pack_manifest_cheats_(manifest_doc_);
}

std::vector<std::uint8_t> SessionOwner::read_manifest_(std::string_view rel) const {
    std::vector<std::uint8_t> buf;
    if (!rel.empty()) {
        auto f = vfs_.open(rel, svc::OpenMode::ReadWhole);
        if (f) {
            auto sz = (*f)->size();
            const cores::IManifestDocRole* role = cores::manifest_doc_role();
            const std::uint64_t kMaxDoc = role != nullptr ? role->doc_max() : 16ull * 1024u * 1024u;
            if (sz && sz->v != 0 && sz->v <= kMaxDoc) {
                buf.resize(static_cast<std::size_t>(sz->v));
                std::size_t off = 0;
                bool ok = true;
                while (off < buf.size()) {
                    auto n = (*f)->read_at(
                        off, std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off)));
                    if (!n || *n == 0) {
                        ok = false;
                        break;
                    }
                    off += *n;
                }
                if (!ok) buf.clear();
            }
        }
    }
    return buf;
}

void SessionOwner::pack_manifest_cheats_(std::span<const std::uint8_t> doc) {
    const cores::IManifestDocRole* role = cores::manifest_doc_role();
    if (role == nullptr) {
        cheat_catalog_ = CheatCatalog{};
        return;
    }
    const std::unique_ptr<cores::ICheatRecords> records = role->cheats_of(infra::as_text(doc));
    pack_cheat_catalog(*records, cheat_catalog_);
}

void SessionOwner::latch_mra_facts_() {
    facts_pending_ = false;
    pending_facts_ = MraFacts{};
    defmra_rel_.clear();
    if (pending_.xml != XmlKind::Mra || pending_.path.empty()) {
        intern_manifest_({});
        return;
    }
    pending_facts_ = mra_facts_at(vfs_, pending_.path.view());
    facts_pending_ = true;
    intern_manifest_(pending_.path.view());
}

bool SessionOwner::push_gating_op_(const proto::LinkOp& op) {
    for (unsigned t = 0; t < kPushRetries; ++t) {
        if (inbox_.push(op)) {
            ++ops_posted_;
            return true;
        }
        sleep_ms_(1);
    }
    ++op_drops_;
    return false;
}

void SessionOwner::order_bind_identity_(const proto::ConfStr& conf) {
    constexpr std::size_t kCap = kLinkTxSlabBytes;
    std::string_view f[4] = {conf.name(), conf.button_list(0), conf.button_list(1),
                             conf.button_list(2)};
    auto total = [&f]() noexcept {
        return f[0].size() + f[1].size() + f[2].size() + f[3].size() + 3;
    };

    for (int i = 3; i >= 1 && total() > kCap; --i)
        f[i] = {};
    if (total() > kCap) f[0] = f[0].substr(0, kCap - 3);
    std::string blob;
    for (unsigned i = 0; i < 4; ++i) {
        blob.append(f[i]);
        if (i != 3) blob.push_back('\0');
    }
    const auto id = inbox_.intern(infra::text_bytes<const std::uint8_t>(blob));
    if (!id) {
        ++identity_intern_refusals_;
        return;
    }
    (void)push_gating_op_(infra::make<proto::LinkOp>(
        proto::LinkOp::BindIdentity{.blob = *id,
                                    .declares_cheats = conf.declares_cheats(),
                                    .declares_turbo = conf.declares_turbo(),
                                    .declares_info = conf.declares_info()}));
}

void SessionOwner::order_bind_doorbells_(const proto::ConfStr& conf) {
    const auto& irqs = conf.irq_bindings();

    const std::size_t rows = std::min(irqs.size(), kMaxDoorbellRows);
    proto::LinkOp::BindDoorbells op{
        .declared = static_cast<std::uint8_t>(std::min<std::size_t>(irqs.size(), 0xFF))};
    if (rows != 0) {
        std::array<std::uint8_t, kLinkTxSlabBytes> packed{};
        std::memcpy(packed.data(), irqs.data(), rows * sizeof(proto::IrqBinding));
        const auto id = inbox_.intern(
            std::span<const std::uint8_t>(packed.data(), rows * sizeof(proto::IrqBinding)));
        if (id) {
            op.table = *id;
            op.rows = static_cast<std::uint8_t>(rows);
        } else {
            ++doorbell_intern_refusals_;
        }
    }
    (void)push_gating_op_(infra::make<proto::LinkOp>(op));
}

void SessionOwner::order_bind_joy_(std::string_view core_name) noexcept {

    (void)push_gating_op_(infra::make<proto::LinkOp>(proto::LinkOp::BindJoy{
        .suppress_analog_followup = cores::profile_for(core_name).suppress_analog_followup}));
}

void SessionOwner::order_bind_facts_() noexcept {
    proto::LinkOp::BindFacts op{.is_arcade = pending_facts_.is_arcade,
                                .vertical = pending_facts_.vertical,
                                .setname_same_dir = pending_facts_.setname_same_dir,
                                .rotation = pending_facts_.rotation};
    if (!pending_facts_.setname.empty()) {
        const auto setname = infra::text_bytes<const std::uint8_t>(pending_facts_.setname);
        const auto id = inbox_.intern(setname);
        if (id) op.setname = *id;
    }
    (void)push_gating_op_(infra::make<proto::LinkOp>(op));
}

std::string_view SessionOwner::make_core_rel_() const noexcept {
    if (pending_.xml == XmlKind::Mra && !pending_.path.empty()) return pending_.path.view();
    return defmra_rel_;
}

void SessionOwner::order_make_core_() {
    manifest_.reset();
    post_make_core_();
}

void SessionOwner::post_make_core_() {
    const std::string_view rel = make_core_rel_();

    const std::string built_from{rel.empty() ? boot_companion_.view() : rel};
    boot_companion_ = PathText{};
    made_with_manifest_ = !built_from.empty();
    if (!made_with_manifest_ || !manifest_rel_.assign(built_from)) manifest_rel_ = PathText{};
    const proto::LinkOp::AdoptCore op = builder_.build(CoreBuilder::Ask{
        .name = core_name_.view(),
        .manifest = {.path = built_from, .text = manifest_text_()},
        .irqs = conf_str_ ? conf_str_->irq_bindings() : std::span<const proto::IrqBinding>{}});

    if (!push_gating_op_(infra::make<proto::LinkOp>(op))) {
        builder_.forget(op);
        give_up_start_(Errc::would_block);
        return;
    }

    if (made_with_manifest_ && conf_str_ && conf_str_->savestate().has_value()) {
        (void)ask_savestates_(manifest_rel_.view(), next_generation());
    }

    if (start_bounded_)
        start_due_ = os::Deadline::in(clock_, std::chrono::milliseconds{start_bound_ms_});
}

std::string_view SessionOwner::manifest_text_() const noexcept {
    if (manifest_doc_.empty()) return {};
    return infra::as_text(std::span(manifest_doc_));
}

void SessionOwner::latch_boot_companion(std::string_view xml) noexcept {
    boot_companion_ = PathText{};
    if (svc::Vfs::extension_matches(xml, "MRA")) (void)boot_companion_.assign(xml);
}

void SessionOwner::elect_defmra_(std::string_view text) {
    defmra_rel_.clear();

    if (pending_.xml == XmlKind::Mra) return;
    pending_facts_ = MraFacts{};
    intern_manifest_({});
    const std::string rel = default_manifest_rel(text);
    if (rel.empty() || !vfs_.file_exists(rel)) return;
    pending_facts_ = mra_facts_at(vfs_, rel);
    defmra_rel_ = rel;
    intern_manifest_(rel);
}

[[nodiscard]] proto::TxSlabId SessionOwner::intern_exact_(std::string_view rel, std::size_t want) {
    if (rel.empty() || want == 0 || want > kLinkTxSlabBytes) return {};
    auto f = vfs_.open(rel, svc::OpenMode::ReadWhole);
    if (!f) return {};
    auto sz = (*f)->size();
    if (!sz || sz->v != want) return {};
    std::uint8_t buf[kLinkTxSlabBytes]{};
    std::size_t off = 0;
    while (off < want) {
        auto n =
            (*f)->read_at(off, std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off)));
        if (!n || *n == 0) return {};
        off += *n;
    }
    const auto id = inbox_.intern(std::span<const std::uint8_t>(buf, want));
    return id ? *id : proto::TxSlabId{};
}

void SessionOwner::intern_saved_cfg_(std::string_view core_name, proto::LinkOp::BindConfig& op) {
    if (core_name.empty()) return;
    std::string rel = "config/";
    rel += core_name;
    rel += ".CFG";
    op.cfg = intern_exact_(rel, 16);
}

void SessionOwner::order_bind_uart_(std::string_view core_name, const proto::ConfStr* conf) {
    proto::LinkOp::BindUart op{.probe_usb_ser = true};
    struct ::stat st {};
    op.usb_ser = ::stat("/dev/ttyUSB0", &st) == 0;
    if (conf != nullptr) {
        std::string_view uart_raw, midi_raw;
        for (const auto& cap : conf->capabilities()) {
            if (cap.kind == proto::Capability::Kind::Uart) {
                op.capable = true;
                uart_raw = cap.raw;
            } else if (cap.kind == proto::Capability::Kind::Midi) {
                midi_raw = cap.raw;
            }
        }
        if (!uart_raw.empty() || !midi_raw.empty()) {
            std::string blob(uart_raw);
            blob.push_back('\0');
            blob.append(midi_raw);
            if (blob.size() <= kLinkTxSlabBytes) {
                const auto id = inbox_.intern(infra::text_bytes<const std::uint8_t>(blob));
                if (id) op.tokens = *id;
            }
        }
    }
    if (!core_name.empty()) {
        std::string rel = "config/uartmode.";
        rel += core_name;
        op.mode = intern_exact_(rel, 4);
        rel = "config/uartspeed.";
        rel += core_name;
        op.speeds = intern_exact_(rel, 12);
    }

    (void)push_gating_op_(infra::make<proto::LinkOp>(op));
}

void SessionOwner::order_hold_reset_() noexcept { order_(proto::LinkOp::HoldReset{}); }

void SessionOwner::order_teardown_() noexcept {
    order_(proto::LinkOp::DropCore{});
    order_bind_decoders_(proto::LinkOp::DecoderTable::Census, gen_);
    order_hold_reset_();
}

template <class A>
void SessionOwner::order_(const A& alt) noexcept {
    if (!inbox_.push(alt)) {
        ++op_drops_;
        return;
    }
    ++ops_posted_;
}

template <class A>
bool SessionOwner::order_refusable_(const A& alt) noexcept {
    if (!inbox_.push(alt)) {
        ++order_refusals_;
        return false;
    }
    ++ops_posted_;
    return true;
}

void SessionOwner::order_bind_decoders_(proto::LinkOp::DecoderTable table,
                                        std::uint32_t gen) noexcept {
    (void)order_refusable_(proto::LinkOp::BindDecoders{.table = table, .gen = as_generation_(gen)});
}

void SessionOwner::publish_save_verdict_(bool ok, CorrelationTag tag) noexcept {
    (void)owner_events_.push(
        infra::make<Event>(Event::InfoRequest{.id = ok ? InfoId::SaveWritten : InfoId::SaveFailed},
                           Event::Head{EmitSite{ERR_SITE()}, {}, tag}));
}

void SessionOwner::order_save_upload_() noexcept {
    if (!inbox_.push(proto::LinkOp::SaveUpload{})) {
        ++op_drops_;
        return;
    }
    ++ops_posted_;
}

bool SessionOwner::order_save_ask_(proto::SaveKind kind, std::uint8_t slot, CorrelationTag tag,
                                   bool gating) noexcept {
    const proto::LinkOp op =
        infra::make<proto::LinkOp>(proto::LinkOp::SaveAsk{.which = kind, .slot = slot, .tag = tag});
    if (gating) return push_gating_op_(op);
    if (!inbox_.push(op)) {
        ++op_drops_;
        return false;
    }
    ++ops_posted_;
    return true;
}

bool SessionOwner::write_durably_(std::string_view rel, std::span<const std::byte> bytes) {
    if (durable_write(vfs_, rel, bytes).has_value()) return true;
    save_write_failures_cell_.publish(++save_write_failures_);
    return false;
}

void SessionOwner::on_save_config_(CorrelationTag tag) {
    if (saved_cfg_stem_.empty()) {
        publish_save_verdict_(false, tag);
        return;
    }
    const auto sample = status_cell_.sample();
    if (sample.generation == 0) {

        publish_save_verdict_(false, tag);
        return;
    }
    std::byte bytes[16];
    for (unsigned i = 0; i < proto::StatusRegister::kWords; ++i) {
        const std::uint16_t w = sample.value.words[i];
        bytes[2 * i] = static_cast<std::byte>(w & 0xFFu);
        bytes[2 * i + 1] = static_cast<std::byte>((w >> 8) & 0xFFu);
    }
    std::string rel = "config/";
    rel.append(saved_cfg_stem_.view());
    rel.append(".CFG");
    const bool ok = write_durably_(rel, std::span<const std::byte>(bytes));

    order_save_upload_();
    publish_save_verdict_(ok, tag);
}

void SessionOwner::publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept {
    if (tag == kUncaused) return;
    (void)owner_events_.push(infra::make<Event>(Event::RequestDone{.which = which},
                                                Event::Head{EmitSite{ERR_SITE()}, {}, tag}));
}

void SessionOwner::publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept {
    (void)owner_events_.push(infra::make<Event>(Event::RequestRefused{.which = which, .why = code},
                                                Event::Head{EmitSite{ERR_SITE()}, {}, tag}));
}

void SessionOwner::advise_switch_failed_(Errc code) noexcept {

    (void)owner_events_.push(infra::make<Event>(Event::SessionAdvisory{.why = code},
                                                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
}

void SessionOwner::refuse_started_switch_(Errc why) noexcept {

    awaiting_conf_str_ = false;
    start_bounded_ = false;
    start_refused_ = true;
    (void)owner_events_.push(infra::make<Event>(
        Event::SessionFailed{.why = why}, Event::Head{EmitSite{ERR_SITE()}, {}, pending_tag_}));
    end_switch_(proto::LinkOp::AbortSwitch{.why = why, .gen = as_generation_(gen_)});
    order_bind_decoders_(proto::LinkOp::DecoderTable::Census, gen_);

    owe_front_end_();
}

void SessionOwner::expect_boot_negotiate() noexcept {
    pending_ = UiRequest::LoadCore{};
    pending_tag_ = kUncaused;
    pending_step_arg_ = StepArg{.loaded = true};
    front_end_fallback_armed_ = true;
    before_seats_ = true;
    start_refused_ = false;
    awaiting_conf_str_ = true;
}

void SessionOwner::on_slot_names_(const proto::LinkEvent::SaveBytes& e) {

    const CorrelationTag tag = e.tag;
    const bool boot = !tag && boot_restore_owed_;
    const bool refresh = !tag && !boot_restore_owed_;
    if (e.status != proto::SaveStatus::Bytes) {
        if (tag) {
            publish_refusal_(UiRequest::Kind::LoadCoreConfig, Errc::not_found, tag);
        }
        if (boot) finish_boot_restore_();
        return;
    }

    const auto path = rx_.bytes(e.rel_path);
    const auto pack = rx_.bytes(e.payload);
    if (!pack.empty()) {
        const std::span<const std::byte> previews = infra::byte_cast<const std::byte>(pack);
        order_slot_previews_(previews.data(), previews.size());
    }
    if (refresh) return;
    const std::uint8_t slot = boot ? 0 : slot_load_slot_;
    proto::LinkOp::BindSlotConfig op{.slot = slot};
    const auto id = InternedFile::config_id(slot);
    if (intern_rel_at_(id, infra::as_text(path))) {
        op.file = id;
    }
    if (!inbox_.push(op)) {
        ++op_drops_;
    } else {
        ++ops_posted_;
    }
    if (boot) finish_boot_restore_();
}

void SessionOwner::finish_boot_restore_() noexcept {
    boot_restore_owed_ = false;
    order_session_up_();
}

void SessionOwner::on_save_bytes_(const proto::LinkEvent::SaveBytes& e) {
    const proto::SaveKind kind = e.which;
    const proto::SaveStatus status = e.status;
    const CorrelationTag tag = e.tag;
    if (status == proto::SaveStatus::Nothing) return;
    if (kind == proto::SaveKind::SlotNames) {
        on_slot_names_(e);
        return;
    }
    if (status != proto::SaveStatus::Bytes) {

        if (tag) publish_save_verdict_(false, tag);
        return;
    }
    const auto path = rx_.bytes(e.rel_path);
    const auto payload = rx_.bytes(e.payload);
    const std::string_view rel = infra::as_text(path);
    const bool ok = write_durably_(rel, std::as_bytes(payload));

    if (tag && !ok) publish_save_verdict_(false, tag);

    if (tag && ok && kind == proto::SaveKind::Slot) {
        ++config_saves_;
        (void)order_save_ask_(proto::SaveKind::SlotNames, slot_save_slot_, CorrelationTag{});
    }
}

void SessionOwner::on(const proto::LinkEvent::ReadyEdge&) noexcept { ++link_event_unhandled_; }

void SessionOwner::on(const proto::LinkEvent::BlockRequest&) noexcept { ++link_event_unhandled_; }

void SessionOwner::on(const proto::LinkEvent::IdentityMatched& e) noexcept {
    on_identity_matched_(e);
}
void SessionOwner::on(const proto::LinkEvent::IdentityMismatch& e) noexcept {
    on_identity_mismatch_(e);
}
void SessionOwner::on(const proto::LinkEvent::ConfStr& e) { on_conf_str_(e); }
void SessionOwner::on(const proto::LinkEvent::SaveBytes& e) { on_save_bytes_(e); }
void SessionOwner::on(const proto::LinkEvent::CoreMade& e) noexcept { on_core_made_(e); }

void SessionOwner::on(const proto::LinkEvent::StartRefused& e) noexcept {

    if (e.bind_gen != as_generation_(gen_) || start_refused_) {
        ++stale_events_;
        return;
    }
    refuse_started_switch_(e.err);
}

void SessionOwner::misrouted(const proto::LinkEvent&) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++link_event_misrouted_;
}

static_assert(LinkEventMainSink<SessionOwner>);

void SessionOwner::on_core_made_(const proto::LinkEvent::CoreMade& m) noexcept {
    if (m.bind_gen != gen_) {
        ++stale_events_;
        return;
    }
    if (start_refused_) {
        ++stale_events_;
        return;
    }
    start_bounded_ = false;
    boot_restore_owed_ = false;
    if (m.made) core_made_once_ = true;
    core_live_ = m.made;
    if (m.made) make_companion_();
    if (!m.made) {

        ++core_make_refusals_;
        refuse_started_switch_(m.err);
        return;
    }
    made_restore_owed_ = m.restore_owed;
    made_cheat_records_ = m.cheat_records;

    if (arm_manifest_()) return;
    finish_core_made_();
}

bool SessionOwner::arm_manifest_() {
    manifest_.reset();
    if (!made_with_manifest_) return false;
    const cores::IManifestDocRole* role = cores::manifest_doc_role();
    if (role == nullptr) return false;
    std::unique_ptr<cores::IManifestStager> stager =
        role->stager_of(manifest_text_(), manifest_rel_.view(), vfs_);
    if (stager == nullptr) return false;
    manifest_.emplace(std::move(stager), manifest_rel_);
    return true;
}

void SessionOwner::step_manifest_() {
    if (!manifest_) return;
    ManifestLadder::Host host{.rung = LoadLadder::Host{inbox_, &ftx_level_cell_, owner_events_},
                              .owner = *this,
                              .map = window_map_,
                              .aperture = aperture_,
                              .windows = window_counts_,
                              .now_ns = clock_.now().count()};
    const std::uint32_t before = inbox_.ring().pushed();
    const ManifestLadder::Pass pass = manifest_->step(host);
    ops_posted_ += static_cast<std::uint32_t>(inbox_.ring().pushed() - before);
    publish_window_counts_();
    if (pass == ManifestLadder::Pass::Failed) {
        const Errc why = manifest_->failure();
        manifest_.reset();
        give_up_start_(why);
        return;
    }
    if (pass != ManifestLadder::Pass::Done) return;

    if (!manifest_->error().empty()) {
        ++manifest_errors_;
        (void)owner_events_.push(
            infra::make<Event>(Event::InfoRequest{.id = InfoId::RomLoadFailed},
                               Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}));
    }
    manifest_.reset();
    finish_core_made_();
}

void SessionOwner::finish_core_made_() noexcept {

    static constexpr CheatCatalog kNoCheats{};
    cheat_catalog_cell_.publish(made_cheat_records_ ? cheat_catalog_ : kNoCheats);
    if (made_restore_owed_) {

        boot_restore_owed_ = true;
        if (order_save_ask_(proto::SaveKind::SlotNames, 0, CorrelationTag{}, true)) return;
        boot_restore_owed_ = false;
    }
    order_session_up_();
}

void SessionOwner::order_session_up_() noexcept {
    const std::vector<RememberedFile> remembered = read_remembered_files_();

    const bool index0_taken = std::ranges::any_of(
        remembered, [](const RememberedFile& f) noexcept { return f.row.ioctl_index == 0; });

    (void)push_gating_op_(infra::make<proto::LinkOp>(proto::LinkOp::SessionUp{}));

    const bool ladder = arm_start_ladder_(index0_taken);
    const std::string rbf = start_rbf_();

    const bool slot0_held = read_remembered_mounts_();
    const StartChain chain = StartChain::plan(
        start_facts_(rbf, index0_taken, slot0_held, !remembered.empty(), ladder), vfs_);

    const bool held = chain.holds_reset() && order_reset_level_(true);
    order_remembered_files_(remembered);
    order_start_chain_(chain);
    if (held) (void)order_reset_level_(false);

    queue_boot_images_(chain);
    arm_next_mount_();
}

std::vector<SessionOwner::RememberedFile> SessionOwner::read_remembered_files_() const {
    std::vector<RememberedFile> out;
    if (!conf_str_ || remembered_stem_.empty()) return out;
    if (remembered_files_ == RememberedFiles::Ignore) return out;
    for (const proto::ConfStrFileRow& row : conf_str_->remembered_rows()) {
        std::string path =
            read_remembered_path(vfs_, remembered_stem_, RememberedSlot::File, row.ioctl_index);
        if (!path.empty()) out.push_back(RememberedFile{row, std::move(path)});
    }
    return out;
}

void SessionOwner::order_remembered_files_(std::span<const RememberedFile> files) {
    for (const RememberedFile& f : files) {
        auto cr = content_for_(f.row, f.path);
        if (!cr) {
            ++remembered_missed_;
            continue;
        }
        cr->whole = true;

        if (f.row.opensave && !vfs_.open(cr->path.view(), svc::OpenMode::Read)) {
            ++remembered_missed_;
            continue;
        }
        const std::uint32_t failed = files_failed_;
        const std::uint32_t drops = op_drops_;
        (void)attach_row_savestates_(f.row.opensave, cr->path.view());
        order_row_content_(*cr, f.row.opensave, UiRequest::SaveChoice::User, kUncaused);
        if (files_failed_ != failed) {
            ++remembered_missed_;
        } else if (op_drops_ == drops) {
            ++remembered_ordered_;
        }
    }
}

StartChain::Facts SessionOwner::start_facts_(std::string_view rbf, bool index0_taken,
                                             bool slot0_held, bool remembered, bool ladder) const {
    const cores::CoreProfile& p = cores::profile_for(core_name_.view());
    return StartChain::Facts{.rule = p.start,
                             .declared = p.start_assets,
                             .home = core_name_.view(),
                             .rbf = rbf,
                             .manifest_load = made_with_manifest_,
                             .generic_identity = generic_identity_,
                             .index0_taken = index0_taken,
                             .remembered = remembered,
                             .ladder = ladder,
                             .slot0_held = slot0_held};
}

std::string SessionOwner::start_rbf_() const {
    if (pending_.path.empty() || pending_.xml != XmlKind::Rbf) return {};
    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    return rel ? std::move(*rel) : std::string{};
}

void SessionOwner::order_start_chain_(const StartChain& chain) {
    const auto files = chain.files();
    for (std::size_t i = 0; i < files.size(); ++i) {
        const StartFile& f = files[i];
        const std::uint32_t failed = files_failed_;
        const std::uint32_t drops = op_drops_;
        if (f.offset != 0) {
            order_rbf_tail_(f.path.view(), f.dest, f.offset);
        } else {
            ContentRequest cr{};
            cr.path = f.path;
            cr.slot = static_cast<std::uint8_t>(f.dest.v);
            cr.whole = chain.holds_reset();

            cr.edge = i + 1 == files.size() ? ContentEdge::ChainEnd : ContentEdge::Silent;
            perform_content_(cr);
        }
        if (files_failed_ == failed && op_drops_ == drops) ++start_assets_ordered_;
    }
}

void SessionOwner::order_rbf_tail_(std::string_view path, proto::WideIoIndex dest,
                                   std::uint64_t offset) {
    auto src = stream_open(vfs_, path, svc::OpenMode::ReadWhole);
    if (!src || src->size < offset) {
        ++files_failed_;
        return;
    }
    if (src->size == offset) {
        ++files_failed_;
        return;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(src->size - offset));
    if (!stream_read(*src->f, offset, std::as_writable_bytes(std::span<std::uint8_t>(buf)))) {
        ++files_failed_;
        return;
    }
    const std::size_t dot = path.rfind('.');
    const std::string_view ext =
        dot == std::string_view::npos ? std::string_view{} : path.substr(dot);
    LoadKey key(buf.size());
    key.feed(buf);
    const auto id = inbox_.intern_file(std::move(buf), ext, path, 0, key.value());
    if (!id) {
        ++files_failed_;
        return;
    }
    (void)inbox_.stamp_edge(*id, ContentEdge::Silent);

    if (!order_content_tx_(*id, ContentWatch{.total = src->size - offset,
                                             .crc = key.value(),
                                             .wire_index = static_cast<std::uint8_t>(dest.v),
                                             .cut_on_hold = true})) {
        ++op_drops_;
        return;
    }
    ++ops_posted_;
    ++files_loaded_;
}

bool SessionOwner::order_reset_level_(bool asserted) {
    return push_gating_op_(infra::make<proto::LinkOp>(proto::LinkOp::WriteStatus{
        .start = proto::StatusBit{0}, .width = 1, .value = asserted ? 1u : 0u}));
}

bool SessionOwner::arm_start_ladder_(bool index0_taken) {
    if (core_name_.empty() || ladder_ != nullptr) return false;
    const auto row = cores::find_core(core_name_.view());
    if (!row || (*row)->make_ladder == nullptr) return false;
    const cores::StagingPolicy& sp = (*row)->profile->staging;
    if (sp.start_save_dir.empty() && (*row)->profile->start_assets.empty()) return false;
    const cores::LadderContext ctx{.vfs = vfs_,
                                   .host = *this,
                                   .clock = clock_,
                                   .image_path = {},
                                   .last_dir = {},
                                   .noreset = false,
                                   .at_core_start = true,
                                   .index0_taken = index0_taken};
    ladder_ = (*row)->make_ladder(*(*row)->profile, ctx);
    if (ladder_ == nullptr) return false;
    start_ladder_ = true;
    ladder_owed_ = Owed{};
    ladder_slot_ = sp.save_slot.v;
    ladder_last_dir_.assign(ladder_->next_last_dir());
    ladder_moved_ = true;
    ++ladders_run_;
    publish_ladder_(true);
    return true;
}

void SessionOwner::on_conf_str_(const proto::LinkEvent::ConfStr& c) {
    if (c.bind_gen.v != gen_) {
        ++stale_events_;
        return;
    }

    if (start_refused_) {
        ++stale_events_;
        return;
    }

    if (c.read == proto::LinkEvent::ConfStrRead::Refused) {
        if (!awaiting_conf_str_) {
            ++link_event_unhandled_;
            return;
        }
        refuse_started_switch_(c.err);
        return;
    }
    const bool awaited = awaiting_conf_str_;
    awaiting_conf_str_ = false;

    std::string raw;
    if (c.chunk0.v != 0) {
        const auto bytes = rx_.bytes(c.chunk0);
        raw.append(infra::as_text(bytes));
    }
    if (c.chunks == 2) {
        const auto bytes = rx_.bytes(c.chunk1);
        raw.append(infra::as_text(bytes));
    }

    constexpr std::size_t cap = sizeof(ConfStrText::text);
    const std::size_t n = raw.size() > cap ? cap : raw.size();
    ConfStrText text{};
    text.truncated = c.read == proto::LinkEvent::ConfStrRead::Truncated;
    text.gen = c.bind_gen;
    text.len = static_cast<std::uint16_t>(n);
    if (n != 0) std::memcpy(text.text, raw.data(), n);
    std::memset(text.text + n, 0, cap - n);
    conf_str_cell_.publish(text);

    proto::LinkOp::BindConfig op{};
    conf_str_.reset();
    pending_ram_image_ = {};
    auto conf = proto::ConfStr::parse(raw, aperture_);
    if (!conf) {

        ++conf_str_parse_refusals_;
        if (awaited) {
            refuse_started_switch_(conf.error().code);
        } else {
            ++link_event_unhandled_;
        }
        return;
    }
    conf_str_gen_ = c.bind_gen;
    const proto::ConfStr& cs = conf_str_.emplace(std::move(*conf));
    const std::string_view name = cs.name();

    (void)core_name_.assign(name);
    order_bind_identity_(cs);
    order_bind_doorbells_(cs);
    order_bind_joy_(name);

    auto parsed = parse_ini_for_core(&vfs_, svc::ConfigSnapshot{}, name, pending_facts_);
    if (parsed) {
        if (replay_ini_) {

            const std::uint8_t dv = svc::direct_video_resolved(*parsed);
            direct_video_ini_cell_.publish(dv);
            parsed->waitmount[0] = '\0';
            if (!strict_direct_video_ && dv != 0) parsed->direct_video = 0;
        }
        if (boot_config_pending_) boot_config_(name, *parsed, op);
        published_conf_ = conf_switches(*parsed);
        config_cell_.publish(*parsed);
        publish_launcher_demand_(name, svc::direct_video_resolved(*parsed) != 0);
    } else {

        op.ini_refused = true;
        op.parse_err = parsed.error().code;
        publish_launcher_demand_(name, false);

        if (replay_ini_) direct_video_ini_cell_.publish(std::uint8_t{0});
    }
    op.conf = published_conf_;
    boot_config_pending_ = false;
    if (cs.savestate().has_value() && !name.empty()) {
        (void)vfs_.ensure_dir(std::string("savestates/") + std::string(name));
    }

    remembered_stem_ = RememberedStem::of(name, pending_facts_);
    elect_defmra_(cs.default_manifest());
    intern_saved_cfg_(effective_name(name, pending_facts_), op);
    op.savestate_dir_ready = cs.savestate().has_value();

    (void)push_gating_op_(infra::make<proto::LinkOp>(op));

    (void)saved_cfg_stem_.assign(effective_name(name, pending_facts_));
    order_bind_facts_();
    order_bind_uart_(effective_name(name, pending_facts_), &cs);

    order_make_core_();
}

void SessionOwner::arm_replay_ini(bool strict) noexcept {
    replay_ini_ = true;
    strict_direct_video_ = strict;
}

void SessionOwner::boot_config_(std::string_view core_name, const svc::ConfigSnapshot& cfg,
                                proto::LinkOp::BindConfig& op) {

    if (cfg.waitmount[0] != '\0' && !cores::profile_for(core_name).is_front_end) {
        if (!wait_for_mount_(cfg.waitmount)) op.waitmount_exhausted = true;
    }
    boot_handoff_.clear();
    if (const LauncherProfile* l = launcher_for_main(cfg.main)) {
        latch_launcher_(*l, core_name);
        return;
    }
    if (auto exe = alternate_executable(vfs_, cfg.main, self_exe_path())) {
        boot_handoff_ = std::move(*exe);
    }
}

void SessionOwner::latch_launcher_(const LauncherProfile& l, std::string_view core_name) {
    launcher_demand_.profile = infra::OptRef<const LauncherProfile>{l};
    const svc::SearchPolicy root{svc::search::kRootOnly, true};
    if (auto p = vfs_.resolve(l.program, root); p) {
        struct ::stat st = {};
        if (::stat(p->c_str(), &st) == 0 && S_ISREG(st.st_mode))
            (void)launcher_demand_.program.assign(*p);
    }
    UiRequest::LoadCore image{.xml = XmlKind::Rbf};
    if (image.path.assign(l.front_end_image) && validate_switch_(image))
        (void)launcher_image_.assign(l.front_end_image);

    launcher_swap_owed_ = !launcher_image_.empty() && cores::profile_for(core_name).is_front_end &&
                          !same_image_name(pending_.path.view(), launcher_image_.view());
}

UiRequest::LoadCore SessionOwner::launcher_alias_(const UiRequest::LoadCore& req) const noexcept {
    if (launcher_image_.empty() || !names_front_end_image(req.path.view(), req.xml)) return req;
    UiRequest::LoadCore out = req;
    (void)out.path.assign(launcher_image_.view());
    return out;
}

void SessionOwner::publish_launcher_demand_(std::string_view core_name,
                                            bool direct_video) noexcept {
    if (!launcher_cell_ || !launcher_demand_.profile) return;

    launcher_demand_.front_end = cores::profile_for(core_name).is_front_end && !launcher_swap_owed_;
    launcher_demand_.direct_video = direct_video;
    launcher_demand_.scanout_core = launcher_demand_.front_end && !launcher_image_.empty() &&
                                    same_image_name(pending_.path.view(), launcher_image_.view());
    ++launcher_demand_.core_gen;
    launcher_cell_->publish(launcher_demand_);
    if (launcher_wake_) launcher_wake_->kick_if_armed();
}

void SessionOwner::withdraw_launcher_demand_() noexcept {
    if (!launcher_cell_ || !launcher_demand_.profile) return;
    if (!launcher_demand_.front_end) return;
    launcher_demand_.front_end = false;
    launcher_demand_.scanout_core = false;
    ++launcher_demand_.core_gen;
    launcher_cell_->publish(launcher_demand_);
    if (launcher_wake_) launcher_wake_->kick_if_armed();
}

void SessionOwner::swap_to_launcher_image_() {
    if (!launcher_swap_owed_ || before_seats_ || switch_standing_ || recovering_) return;
    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps || !park_.ask_room()) return;
    launcher_swap_owed_ = false;
    UiRequest::LoadCore req{.xml = XmlKind::Rbf};
    if (!req.path.assign(launcher_image_.view())) return;
    (void)ask_switch_(req, kUncaused);
}

bool SessionOwner::wait_for_mount_(std::string_view needle) {
    constexpr unsigned kPollMs = 1000;
    constexpr unsigned kBudgetMs = 60'000;
    for (unsigned waited = 0;; waited += kPollMs) {
        if (mount_present(needle)) return true;
        if (waited >= kBudgetMs) return false;
        sleep_ms_(kPollMs);
    }
}

void SessionOwner::on_identity_mismatch_(const proto::LinkEvent::IdentityMismatch& w) noexcept {
    ++identities_fail_;
    if (w.bind_gen.v != gen_) return;
    if (w.at_accept && !awaiting_identity_ && awaiting_conf_str_) {

        if (w.err == Errc::fpga_not_ready && core_made_once_) {
            awaiting_conf_str_ = false;
            start_bounded_ = false;
            ++fabric_wedges_;

            order_bind_decoders_(proto::LinkOp::DecoderTable::Census, gen_);
            settle_reboot_(true);
            return;
        }
        refuse_started_switch_(w.err);
        return;
    }
    if (awaiting_identity_ && identity_poll_budget_ != 0 &&
        ++identity_tries_ > identity_poll_budget_) {
        give_up_start_(w.err);
    }
}

void SessionOwner::on_identity_matched_(const proto::LinkEvent::IdentityMatched& w) noexcept {
    ++identities_ok_;
    if (w.bind_gen.v != gen_) return;
    generic_identity_ = proto::fold_core_type(w.type_byte) == proto::CoreType::EightBit;

    if (!awaiting_identity_) {
        link_wide_ = w.wide;
        return;
    }
    awaiting_identity_ = false;
    identity_tries_ = 0;

    const BindOutcome bind = bind_link_rows(kLinkRows, inbox_, gen_);
    ops_posted_ += bind.posted;
    order_refusals_ += bind.decoders_refused;
    op_drops_ += bind.slots_refused;
    if (facts_pending_) {
        order_bind_facts_();
        facts_pending_ = false;
    }
    end_switch_(
        proto::LinkOp::ApplyCore{.loaded = pending_step_arg_.loaded, .gen = as_generation_(gen_)});
    awaiting_conf_str_ = true;
}

bool SessionOwner::validate_switch_(const UiRequest::LoadCore& req) {
    if (req.path.empty()) return true;
    auto p = resolve_bitstream(vfs_, req.path.view(), req.xml);
    if (!p) return false;
    auto f = vfs_.open(*p, svc::OpenMode::Read);
    return f.has_value();
}

void SessionOwner::on_load_core_(const UiRequest::LoadCore& req, CorrelationTag tag) {
    const auto refuse = [&](Errc code) {
        ++load_core_refusals_;
        publish_refusal_(UiRequest::Kind::LoadCore, code, tag);
    };

    if (req.xml >= XmlKind::Mgl) {
        refuse(Errc::bad_format);
        return;
    }
    if (switch_standing_ || recovering_ || reboot_failed_ || reboot_owed_) {
        refuse(Errc::negotiation);
        return;
    }

    const std::optional<HeldLoad> prior = std::exchange(held_load_, std::nullopt);
    const Asked asked = ask_switch_(req, tag);
    if (asked == Asked::Invalid) {
        held_load_ = prior;
        return;
    }
    if (prior) {
        ++loads_cancelled_;
        publish_refusal_(UiRequest::Kind::LoadCore, Errc::cancelled, prior->tag);
    }

    if (asked == Asked::NoRoom) {
        held_load_.emplace(HeldLoad{.req = req, .tag = tag});
        ++loads_held_;
    }
}

void SessionOwner::retry_held_load_() {
    if (!held_load_ || switch_standing_ || recovering_) return;
    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps || !park_.ask_room()) return;
    const HeldLoad h = *held_load_;
    held_load_.reset();
    on_load_core_(h.req, h.tag);
}

void SessionOwner::ask_switch_or_refuse_(const UiRequest::LoadCore& req, CorrelationTag tag) {
    if (ask_switch_(launcher_alias_(req), tag) == Asked::NoRoom)
        publish_refusal_(UiRequest::Kind::LoadCore, Errc::would_block, tag);
}

SessionOwner::Asked SessionOwner::ask_switch_(const UiRequest::LoadCore& req, CorrelationTag tag) {

    if (!validate_switch_(req)) {

        advise_switch_failed_(Errc::not_found);
        return Asked::Invalid;
    }

    if (req.path.empty() && fabric_unconfigured_()) {
        advise_switch_failed_(Errc::fpga_not_ready);
        return Asked::Invalid;
    }

    if (proto::kLinkTxCapacity - inbox_.ring().size() < kTeardownOps) {
        ++order_refusals_;
        return Asked::NoRoom;
    }

    const std::uint32_t next = (gen_ >= kMaxGen) ? 1u : gen_ + 1u;
    ++switches_asked_;
    if (!park_.post_ask(Quiesce{next})) {
        ++ask_refusals_;
        return Asked::NoRoom;
    }
    gen_ = next;
    generic_identity_ = false;
    recover_owed_ = false;
    front_end_owed_ = false;
    front_end_fallback_armed_ = !names_front_end_image(req.path.view(), req.xml);
    start_bounded_ = false;
    start_refused_ = false;
    switch_programmed_ = false;

    ready_watch_armed_ = false;

    if (last_order_ && !switch_standing_) {
        last_order_.reset();
        ++orders_superseded_;
    }
    withdraw_launcher_demand_();

    pause_seats_(gen_, false);
    receipt_.reset();
    drop_ladder_();
    loader_memo_ = cores::LoaderMemo{};
    awaiting_conf_str_ = false;
    pending_ = req;
    pending_tag_ = tag;
    boot_companion_ = PathText{};

    forget_core_();
    order_teardown_();
    switch_standing_ = true;
    note_memsz_(req);
    return Asked::Yes;
}

template <class A>
void SessionOwner::end_switch_(A alt) noexcept {
    alt.tag = pending_tag_;
    last_order_ = infra::make<proto::LinkOp>(alt);

    if (!push_last_order_()) ++order_refusals_;
}

bool SessionOwner::push_last_order_() noexcept {
    if (!last_order_) return true;
    if (!inbox_.push(*last_order_)) return false;
    ++ops_posted_;
    last_order_.reset();
    switch_standing_ = false;
    resume_seats_();
    return true;
}

void SessionOwner::pause_seats_(std::uint32_t gen, bool at_receipt) noexcept {
    for (std::size_t i = 0; i < pauses_.size(); ++i) {
        if (pauses_[i] == nullptr) continue;
        if (pauses_at_receipt(static_cast<SeatTag>(i + 1)) == at_receipt) pauses_[i]->pause(gen);
    }
}

void SessionOwner::resume_seats_() noexcept {
    for (xthread::PauseLatch* l : pauses_)
        if (l != nullptr) l->resume();
}

bool SessionOwner::seats_paused_(std::uint32_t gen) const noexcept {
    for (const xthread::PauseLatch* l : pauses_)
        if (l != nullptr && !l->paused_at(gen)) return false;
    return true;
}

bool SessionOwner::program_owed() const noexcept {
    return receipt_.has_value() && !recovering_ && seats_paused_(gen_);
}

int SessionOwner::wake_hint_ms(int floor_ms) const noexcept {
    const auto until = [this](const os::Deadline& d, int floor) noexcept {
        const std::int64_t left_ns = (d.at() - clock_.now()).count();
        if (left_ns <= 0) return 0;
        const std::int64_t ms = (left_ns + 999'999) / 1'000'000;
        return ms < floor ? static_cast<int>(ms) : floor;
    };

    if (reboot_owed_) {
        const int f = reboot_owed_->ordered ? floor_ms : std::min(floor_ms, kLastOrderRetryMs);
        return until(reboot_due_, f);
    }
    const bool owed = held_load_ || (front_end_owed_ && !before_seats_);
    const bool ask_due = owed && !switch_standing_ && !recovering_;
    if (last_order_ || ask_due) return floor_ms < kLastOrderRetryMs ? floor_ms : kLastOrderRetryMs;

    if (rung_wait_() != RungWait::None && floor_ms > kPieceRetryMs) floor_ms = kPieceRetryMs;
    if (start_bounded_ && start_bound_ms_ != 0) floor_ms = until(start_due_, floor_ms);
    if (!receipt_ || recovering_) return floor_ms;
    return until(pause_due_, floor_ms);
}

unsigned SessionOwner::take_park_acks_() {
    unsigned taken = 0;
    while (const auto receipt = park_.take_ack()) {
        ++taken;
        if (receipt->gen() != gen_) {
            ++stale_acks_;
            continue;
        }

        receipt_.emplace(*receipt);

        pause_seats_(gen_, true);
        pause_due_ = os::Deadline::in(clock_, std::chrono::nanoseconds{kPauseDeadlineNs});
    }
    return taken;
}

void SessionOwner::try_program_() {
    if (!receipt_ || recovering_) return;
    const bool all = seats_paused_(gen_);
    if (!all && !pause_due_.expired(clock_)) return;

    if (!all) pause_expiries_cell_.publish(++pause_expiries_);
    const ParkReceipt r = *receipt_;
    receipt_.reset();
    (void)::unlink("/tmp/GAMEID");

    (void)::unlink("/tmp/CONFSTR");

    builder_.drop_windows(r);
    const Programmed p = program_(r);
    pending_step_arg_ = step_arg_(p);

    if (p == Programmed::Ok) {
        switch_programmed_ = true;
        recover_owed_ = false;
    }

    start_bounded_ = true;
    start_due_ = os::Deadline::in(clock_, std::chrono::milliseconds{start_bound_ms_});
    if (p == Programmed::Failed) {
        awaiting_identity_ = false;
        end_switch_(proto::LinkOp::ApplyCore{.loaded = pending_step_arg_.loaded,
                                             .gen = as_generation_(gen_)});
    } else {
        order_(proto::LinkOp::ReleaseReset{});
        order_bind_decoders_(proto::LinkOp::DecoderTable::PreSession, gen_);
        identity_tries_ = 0;
        awaiting_identity_ = true;
        link_wide_ = false;
    }
}

SessionOwner::Programmed SessionOwner::program_(const ParkReceipt& receipt) {

    if (pending_.path.empty()) return Programmed::NotNeeded;

    auto rel = resolve_bitstream(vfs_, pending_.path.view(), pending_.xml);
    if (!rel) {
        ++programs_failed_;
        return Programmed::Failed;
    }
    ++programs_run_;
    const bool ok = programmer_.program(receipt, *rel).has_value();
    if (!ok) ++programs_failed_;
    load_ok_ = ok;
    ++fabric_gen_;
    if (ok) latch_mra_facts_();
    return ok ? Programmed::Ok : Programmed::Failed;
}

SessionOwner::StepArg SessionOwner::step_arg_(Programmed p) const noexcept {

    if (p == Programmed::NotNeeded) return StepArg{.loaded = true};
    return StepArg{.loaded = p == Programmed::Ok};
}

void SessionOwner::settle_reboot_(bool cold) {

    if (reboot_owed_) return;
    const std::uint16_t seq = ++reboot_seq_;
    const proto::LinkOp::RebootNow quiesce{.cold = cold, .quiesce = true, .seq = seq};
    reboot_owed_ = OwedReboot{.cold = cold, .ordered = order_refusable_(quiesce), .seq = seq};
    reboot_due_ = os::Deadline::in(clock_, std::chrono::nanoseconds{kRebootDrainBoundNs});
}

bool SessionOwner::watch_reboot_() {
    if (!reboot_owed_) return false;
    if (!reboot_owed_->ordered) {

        reboot_owed_->ordered = inbox_.push(proto::LinkOp::RebootNow{
            .cold = reboot_owed_->cold, .quiesce = true, .seq = reboot_owed_->seq});
        if (reboot_owed_->ordered) ++ops_posted_;
    }
    if (!reboot_due_.expired(clock_)) return true;

    ++reboot_drain_expiries_;
    finish_reboot_();
    return false;
}

void SessionOwner::on(const proto::LinkEvent::RebootQuiesced& e) {

    if (!reboot_owed_ || !reboot_owed_->ordered || e.seq != reboot_owed_->seq) {
        ++stale_events_;
        return;
    }
    finish_reboot_();
}

void SessionOwner::finish_reboot_() {
    const bool cold = reboot_owed_ ? reboot_owed_->cold : true;
    reboot_owed_.reset();
    ++reboots_settled_;
    (void)reboot_.reboot(cold);

    (void)order_refusable_(proto::LinkOp::RebootNow{.cold = cold, .quiesce = false});
    reboot_failed_ = true;
}

void SessionOwner::sleep_ms_(unsigned ms) {
    if (ops_.sleeper) {
        ops_.sleeper->sleep_ms(ms);
        return;
    }
    ::usleep(static_cast<::useconds_t>(ms) * 1000u);
}

}  // namespace mister::app
