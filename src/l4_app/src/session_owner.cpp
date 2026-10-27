// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_owner.h"

#include <fcntl.h>

#include <chrono>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "app/xml_kind.h"
#include "app/conf_str_cell.h"
#include "app/owner_push.h"
#include "app/durable_write.h"
#include "app/hd_look.h"
#include "app/durable_write_service.h"
#include "app/ini_parse.h"
#include "app/link_event_dispatch.h"
#include "app/link_rows.h"
#include "app/mra_facts.h"
#include "app/pending_load.h"
#include "app/remembered_path.h"
#include "app/sdram_size_code.h"
#include "app/stream_io.h"
#include "app/ui_admission.h"
#include "app/ui_request_dispatch.h"
#include "app/config_apply.h"
#include "app/event.h"
#include "cores/boot_asset.h"
#include "cores/ladder_context.h"
#include "cores/payload_pieces.h"
#include "cores/addressed_load.h"
#include "cores/manifest_doc_role.h"
#include "cores/registry.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "hal/boot_handoff.h"
#include "proto/conf_str.h"
#include "proto/core_session.h"
#include "proto/link_op.h"
#include "svc/file.h"
#include "svc/search_policy.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

[[nodiscard]] proto::BindGeneration as_generation(std::uint32_t gen) noexcept {
    return proto::BindGeneration{static_cast<std::uint16_t>(gen)};
}

}

constexpr int kLastOrderRetryMs = 1;

constexpr int kPieceRetryMs = 5;

static_assert(
    cores::PayloadPieces::kAhead + InternedFile::kHeldOps + 1 < InternedFile::kMaxLiveFiles
);

SessionOwner::SessionOwner(QuiesceChannel& park, const Link& link) noexcept
    : park_(park),
      inbox_(link.inbox),
      rx_(link.rx),
      ini_(link.config_cell, link.storage, link.ops.sleeper),
      owner_events_(link.owner_events),
      cheat_catalog_cell_(link.cheat_catalog_cell),
      ui_requests_(link.main_wake),
      status_cell_(link.status_cell),
      mount_status_cell_(link.mount_status_cell),
      save_extent_cell_(link.save_extent_cell),
      clock_(
          link.clock.has_value() ? static_cast<const os::IClock&>(*link.clock)
                                 : static_cast<const os::IClock&>(default_clock_)
      ),
      start_watch_(clock_),
      programmer_(link.programmer),
      readiness_(link.pin_levels, programmer_),
      vfs_(link.storage),
      ladder_(inbox_, link.ladder_cell, tally_, vfs_, clock_),
      negotiated_(rx_, link.conf_str_cell, vfs_, inbox_),
      mounts_(
          MountQueue::Wiring{
              .vfs = vfs_,
              .inbox = inbox_,
              .events = owner_events_,
              .clock = clock_,
              .ladder = ladder_,
              .negotiated = negotiated_,
              .owner_tally = tally_
          }
      ),
      launcher_(vfs_),
      cell_(link.fabric_cell),
      aperture_(link.aperture),
      ops_(link.ops),
      bind_(inbox_, tally_, vfs_, ops_.sleeper),
      saves_(
          ConfigSaves::Wiring{
              .inbox = inbox_,
              .rx = rx_,
              .tally = tally_,
              .owner_events = owner_events_,
              .vfs = vfs_,
              .status_cell = status_cell_,
              .negotiated = negotiated_,
              .sleeper = ops_.sleeper
          }
      ),
      companion_(vfs_, negotiated_, tally_, aperture_.phys.v, link.companion_binds),
      window_map_(link.window_map),
      ftx_level_cell_(link.file_tx_level_cell),
      content_(
          ContentPick::Wiring{
              .inbox = inbox_,
              .ftx_level_cell = ftx_level_cell_,
              .events = owner_events_,
              .vfs = vfs_,
              .window_map = window_map_,
              .negotiated = negotiated_,
              .mounts = mounts_,
              .ladder = ladder_,
              .rungs = rungs_,
              .owner_tally = tally_,
              .mgl_row0_cell = mgl_row0_cell_,
              .act_keys = act_keys_,
              .status_cell = status_cell_,
              .boot_restore = boot_restore_,
              .aperture = aperture_,
              .windows = windows_
          }
      ),
      save_state_asks_(link.save_state_asks),
      save_state_facts_(link.save_state_facts),
      builder_(
          link.bay,
          link.bay_releases,
          link.core_terms,
          link.doorbell_source,
          CoreBuilder::SectorCells{
              .grant = link.sector_grant,
              .route = link.sector_route,
              .wake = link.io_wake
          },
          CoreBuilder::OsdCells{.grant = link.osd_grant, .route = link.osd_route},
          CoreBuilder::SysCells{
              .grant = link.sys_grant,
              .route = link.sys_route,
              .wake = link.io_wake
          }
      ),
      reboot_(clock_, inbox_, tally_, link.boot_handoff, link.ops.reset, link.ops.sleeper),
      switch_(
          CoreSwitch::Wiring{
              .park = park_,
              .inbox = inbox_,
              .clock = clock_,
              .programmer = programmer_,
              .vfs = vfs_,
              .builder = builder_,
              .readiness = readiness_,
              .reboot = reboot_,
              .start_watch = start_watch_,
              .events = owner_events_,
              .owner_tally = tally_
          }
      ),
      roster_(
          RungEdges{
              .inbox = inbox_,
              .level = ftx_level_cell_,
              .events = owner_events_,
              .window_map = window_map_,
              .aperture = aperture_,
              .vfs = vfs_,
              .windows = windows_,
              .clock = clock_
          }
      ) {
    (void)front_end_image_.assign(kFrontEndImage);
    handoff_ = link.boot_handoff;
    launcher_.bind(link.launcher_demand, link.launcher_wake);
    publish_fabric_();
}

void SessionOwner::set_identity_poll_budget(std::uint32_t n) noexcept {
    start_watch_.set_identity_poll_budget(n);
}

void SessionOwner::set_start_bound_ms(std::uint32_t ms) noexcept {
    start_watch_.set_start_bound_ms(ms);
}

void SessionOwner::set_readiness_poll_budget(std::uint32_t n) noexcept {
    readiness_.set_readiness_poll_budget(n);
}

void SessionOwner::set_replay_save_root(std::string_view rel) noexcept {
    (void)replay_save_root_.assign(rel);
}

void SessionOwner::grant_pause(SeatTag s, xthread::PauseLatch& latch) noexcept {
    switch_.grant_pause(s, latch);
}

void SessionOwner::on_seats_live() noexcept {
    start_watch_.on_seats_live();
}

infra::OptRef<const LauncherProfile> SessionOwner::launcher() const noexcept {
    return launcher_.profile();
}

const SessionOwner::Count& SessionOwner::pause_expiries_cell() const noexcept {
    return switch_.pause_expiries_cell();
}

const SessionOwner::Count& SessionOwner::recover_polls_cell() const noexcept {
    return readiness_.recover_polls_cell();
}

const xthread::Telemetry<FallbackCounts, SeatTag::Main>& SessionOwner::
    fallback_cell() const noexcept {
    return start_watch_.fallback_cell();
}

const LinkTxChannel<SeatTag::Main>& SessionOwner::inbox() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return inbox_;
}

LinkTxChannel<SeatTag::Main>& SessionOwner::inbox() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return inbox_;
}

bool SessionOwner::ladder_live() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return ladder_.live();
}

bool SessionOwner::switch_standing() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return switch_.switch_standing();
}

bool SessionOwner::reboot_owed() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return reboot_.owed().has_value();
}

const std::optional<proto::ConfStr>& SessionOwner::conf_str() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return negotiated_.conf_str();
}

const RememberedStem& SessionOwner::remembered_stem() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return negotiated_.remembered_stem();
}

unsigned SessionOwner::tick() {

    builder_.reap();

    if (reboot_.watch()) {
        publish_fabric_();
        return 0;
    }

    (void)switch_.push_last_order();

    switch_.retry_held_load(*this);

    roster_.step_all(*this);

    const unsigned acks = switch_.take_park_acks();
    if (const auto gen = switch_.try_program(*this))
        release_and_await_identity_(*gen);

    watch_start_();
    watch_bridge_();
    fall_back_to_front_end_();
    if (const auto req = launcher_.swap_to_image(launcher_swap_held_(), launcher_swap_room_()))
        (void)switch_.ask(*req, kUncaused, *this);

    const ReadinessWatch::SwitchView sw{
        .standing = switch_.switch_standing(),
        .programmed = switch_.switch_programmed()
    };
    if (const auto reboot = readiness_.watch_readiness(sw, *this))
        reboot_.settle(reboot->cold);

    publish_fabric_();
    return acks;
}

template <class A>
bool SessionOwner::admit_(const A& req, CorrelationTag tag) noexcept {
    constexpr UiAdmission row = ui_admission<A>();
    static_assert(!row.bespoke, "a bespoke arm runs its own prelude");
    if (row.gate_switch && switch_.refuse_in_switch(A::kKind, tag))
        return false;
    if (row.gate_load && refuse_in_load_(A::kKind, tag))
        return false;
    if constexpr (row.scoped != UiAdmission::Scope::None) {
        const bool live = row.scoped == UiAdmission::Scope::LiveSession;
        std::optional<Errc> why;
        if (live && !negotiated_.core_live())
            why = Errc::core_load;
        else if ((live && !negotiated_.conf_str()) || !negotiated_.in_scope(req.scope))
            why = Errc::stale;
        if (!why)
            return true;
        if (row.refuse_via == UiAdmission::Refusal::FileAsk)
            refuse_file_ask(A::kKind, tag, *why);
        else
            publish_refusal(A::kKind, *why, tag);
        return false;
    }
    return true;
}

void SessionOwner::on(const UiRequest::LoadCore& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    switch_.on_load_core(launcher_.alias(req), head.tag, *this);
}

void SessionOwner::on(const UiRequest::SaveConfig& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    saves_.save_config(head.tag);
}

void SessionOwner::on(const UiRequest::SaveDips& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    saves_.save_dips(head.tag);
}

void SessionOwner::on(const UiRequest::SaveCoreConfig& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    saves_.save_slot(req.slot.v, head.tag);
}

void SessionOwner::on(const UiRequest::LoadCoreConfig& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    saves_.load_slot(req.slot.v, head.tag);
}

void SessionOwner::on(const UiRequest::LoadFile& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;

    finish_pick_(
        UiRequest::LoadFile::kKind,
        head.tag,
        content_.load_file(UiRequest::LoadFile::kKind, req, head.tag, negotiated_.take_ram_image())
    );
}

void SessionOwner::on(const UiRequest::LoadFileByDigit& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    finish_pick_(
        UiRequest::LoadFileByDigit::kKind,
        head.tag,
        content_.load_file(
            UiRequest::LoadFileByDigit::kKind,
            req,
            head.tag,
            negotiated_.take_ram_image()
        )
    );
}

void SessionOwner::on(const UiRequest::LoadRamImage& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    if (req.recipe.size() == 0) {
        ++tally_.ram_images_declined;
        (void)owner_events_.push(
            infra::make<Event>(
                Event::RamImageDeclined{.why = Errc::bad_format},
                Event::Head{EmitSite{ERR_SITE()}, {}, head.tag}
            )
        );
        return;
    }

    negotiated_.hold_ram_image(req.recipe);
}

void SessionOwner::on(const UiRequest::MountImage& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    mounts_.on_mount_image(
        *this,
        MountAsk{UiRequest::MountImage::kKind, req.index, req.path, head.tag}
    );
}

void SessionOwner::on(const UiRequest::UnmountImage& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    mounts_.on_mount_image(
        *this,
        MountAsk{UiRequest::UnmountImage::kKind, req.index, PathText{}, head.tag}
    );
}

void SessionOwner::on(const UiRequest::SetOsdStyle& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    saves_.set_ini_key("osd_style", req.style != 0 ? "1" : "0");
}

void SessionOwner::on(const UiRequest::SetHdLook& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    const std::string_view key = hd_look_ini_key(req.field);
    if (key.empty())
        return;
    saves_.set_ini_key(key, std::to_string(req.value));
}

void SessionOwner::on(const UiRequest::SdramDetected& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    const std::uint8_t code = sdram_size_code(req.osd_mask);

    if (handoff_ && !handoff_->write_sdram_cfg(code))
        ++tally_.sdram_write_failures;
    set_boot_cookie(static_cast<std::uint16_t>(kSdramCookieMeasured | code));
}

void SessionOwner::set_boot_cookie(std::uint16_t c) {
    cookie_ = c;
    publish_fabric_();
}

void SessionOwner::note_memsz_(const UiRequest::LoadCore& req) noexcept {
    if (req.path.empty() || names_front_end_image(req.path.view(), req.xml))
        return;
    if ((cookie_ & kSdramCookieMeasured) == 0)
        ++tally_.memsz_unmeasured;
}

static_assert(UiRequestSink<SessionOwner>);

void SessionOwner::on(const UiRequest::ResetCore& req, const UiRequest::Head& head) {
    constexpr UiRequest::Kind kKind = UiRequest::ResetCore::kKind;
    if (switch_.refuse_in_switch(kKind, head.tag))
        return;
    if (!negotiated_.core_live() || !negotiated_.conf_str())
        return publish_refusal(kKind, Errc::core_load, head.tag);
    if (!negotiated_.in_scope(req.scope))
        return publish_refusal(kKind, Errc::stale, head.tag);
    if (refuse_in_load_(kKind, head.tag))
        return;
    if (ladder_.live() || mounts_.save_mount_live() || reset_owed_.has_value())
        return publish_refusal(kKind, Errc::would_block, head.tag);

    const bool clears = req.edge.toggles_reset_bit();
    const proto::LinkOp::CoreReset op{.edge = req.edge};
    const auto row = cores::find_core(negotiated_.core_name().view());
    if (row && (*row)->profile->reset.ejects(req.edge)) {
        const bool carried = ladder_.noreset();
        if (clears)
            ladder_.clear_noreset();
        mounts_.arm_mount(
            *this,
            MountAsk{
                UiRequest::Kind::UnmountImage,
                (*row)->profile->staging.disc_slot,
                PathText{},
                head.tag
            }
        );
        if (!ladder_.live()) {
            ladder_.restore_noreset(carried);
            return;
        }
        reset_owed_ = op;
        return;
    }

    if (row && (*row)->profile->reset.ladders && (*row)->profile->reset.reacts(req.edge)) {
        if (!ladder_.arm_core(req.edge, {}, **row, *this, Owed{kKind, head.tag}))
            publish_refusal(kKind, Errc::core_load, head.tag);
        return;
    }
    if (order_reset_(op, head.tag) && clears)
        ladder_.clear_noreset();
}

bool SessionOwner::order_reset_(const proto::LinkOp::CoreReset& op, CorrelationTag tag) noexcept {
    if (!order(op)) {
        publish_refusal(UiRequest::ResetCore::kKind, Errc::would_block, tag);
        return false;
    }
    ++tally_.resets_ordered;
    return true;
}

void SessionOwner::on(const UiRequest::Reboot& req, const UiRequest::Head& head) {
    if (!admit_(req, head.tag))
        return;
    reboot_.settle(true);
}

void SessionOwner::misrouted(const UiRequest&) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++tally_.ui_request_misrouted;
}

bool SessionOwner::refuse_in_load_(UiRequest::Kind kind, CorrelationTag tag) noexcept {
    const bool rom_waits = mounts_.save_holds_load();
    if (!rungs_.any_live(kLoadBelt) && !rom_waits)
        return false;
    ++tally_.loads_refused_busy;
    publish_refusal(kind, Errc::would_block, tag);
    return true;
}

void SessionOwner::watch_start_() noexcept {

    if (start_watch_.bound_expired())
        give_up_start_(Errc::timeout);
}

void SessionOwner::watch_bridge_() noexcept {

    if (switch_.switch_standing() || readiness_.recovering() || start_watch_.refused())
        return;
    if (builder_.bridge_refused())
        end_bridge_session_();
}

void SessionOwner::end_bridge_session_() noexcept {
    start_watch_.end_by_bridge(switch_.gen());
    (void)owner_events_.push(
        infra::make<Event>(
            Event::SessionFailed{.why = Errc::bridge_state},
            Event::Head{EmitSite{ERR_SITE()}, {}, switch_.pending_tag()}
        )
    );
    start_watch_.owe_front_end_always(switch_.pending());
}

void SessionOwner::give_up_start_(Errc why) noexcept {

    start_watch_.give_up(why);
    refuse_started_switch_(why);
}

void SessionOwner::fall_back_to_front_end_() {
    if (!start_watch_.front_end_askable(switch_.switch_standing() || readiness_.recovering()))
        return;
    if (reboot_.failed()) {
        start_watch_.drop_front_end();
        return;
    }

    if (proto::kLinkTxCapacity - inbox_.ring().size() < CoreSwitch::kTeardownOps
        || !park_.ask_room())
        return;
    const UiRequest::LoadCore req{.path = front_end_image_, .xml = XmlKind::Rbf};
    switch (switch_.ask(req, kUncaused, *this)) {
        case CoreSwitch::Asked::Yes:
            start_watch_.front_end_asked();
            break;
        case CoreSwitch::Asked::Invalid:
            start_watch_.front_end_invalid();
            break;
        case CoreSwitch::Asked::NoRoom:
            return;
    }
    start_watch_.publish_fallbacks();
}

void SessionOwner::publish_fabric_() {
    FabricState st{};
    st.gen = fabric_gen_;
    st.cookie = cookie_;
    st.configured = programmer_.programmed();
    st.bridges_down = programmer_.bridges_down();
    st.load_ok = readiness_.load_ok();
    cell_.publish(st);
}

void SessionOwner::service_boot(const UiRequest::LoadCore& req) {

    publish_fabric_();
    note_memsz_(req);
    switch_.latch_pending(req, kUncaused);

    start_watch_.arm_boot_fallback(req);
    order_hold_reset_();
    order_bind_decoders_(proto::LinkOp::DecoderTable::PreSession, kBootGen);

    const CoreSwitch::StepArg arg = switch_.step_arg(switch_.program_at_boot(*this));
    switch_.latch_step_arg(arg);
    (void)order_refusable_(
        proto::LinkOp::ApplyCore{.loaded = arg.loaded, .gen = as_generation(kBootGen)}
    );
    start_watch_.expect_conf_str(arg.loaded);
    if (negotiated_.facts_pending()) {
        bind_.facts(negotiated_.pending_facts());
        negotiated_.facts_sent();
    }

    publish_fabric_();
}

void SessionOwner::refuse_file_ask(UiRequest::Kind asked, CorrelationTag tag, Errc code) noexcept {
    TASTY_SEAT_BODY(SessionOwner);

    publish_refusal(asked, code, tag);
    ++tally_.files_failed;
}

void SessionOwner::finish_pick_(
    UiRequest::Kind asked,
    CorrelationTag tag,
    const ContentPick::Out& out
) {
    if (out.refuse)
        return refuse_file_ask(asked, tag, *out.refuse);
    if (out.run)
        run_load_(*out.run);
}

void SessionOwner::run_load_(const PickedLoad& load) {
    if (const auto gen = attach_row_savestates(load.opensave, load.content.path.view())) {
        hold_for_savestates(load, *gen, clock_.now().count() + cores::kSaveStateHoldBoundNs);
        return;
    }
    after_content_(content_.run_attached_load(load, *this));
}

void SessionOwner::step_savestate_hold_() {
    if (!rungs_.ss_hold)
        return;
    if (!savestates_answered_(rungs_.ss_hold->gen)) {
        if (clock_.now().count() < rungs_.ss_hold->due_ns)
            return;
        ++tally_.savestate_hold_expiries;
    }
    const PickedLoad load = rungs_.ss_hold->load;
    rungs_.ss_hold.reset();
    after_content_(content_.run_attached_load(load, *this));
}

bool SessionOwner::content_ordered_() const noexcept {
    const bool rom_waits = mounts_.save_holds_load();
    return !content_.picked_load() && !rungs_.any_live(kContentOwing) && !rom_waits;
}

std::optional<std::uint32_t> SessionOwner::attach_row_savestates(
    bool opensave,
    std::string_view rom
) {
    TASTY_SEAT_BODY(SessionOwner);
    const auto& conf = negotiated_.conf_str();
    if (!opensave || !conf || !conf->savestate().has_value())
        return std::nullopt;
    const std::uint32_t gen = next_generation();
    if (!ask_savestates_(rom, gen))
        return std::nullopt;
    return gen;
}

void SessionOwner::hold_for_savestates(
    const PickedLoad& load,
    std::uint32_t gen,
    std::int64_t due_ns
) {
    TASTY_SEAT_BODY(SessionOwner);
    rungs_.ss_hold = RungSet::SaveStateHold{.load = load, .gen = gen, .due_ns = due_ns};
}

bool SessionOwner::ask_savestates_(std::string_view rom, std::uint32_t gen) noexcept {
    const bool service = save_state_asks_
        && save_state_facts_
        && save_state_facts_->generation() != 0;
    SaveStateAttach::Ask ask{
        .decl = *negotiated_.conf_str()->savestate(),
        .core_name = negotiated_.core_name(),
        .arcade = negotiated_.made_with_manifest(),
        .generation = gen
    };
    if (!service || !ask.rom_path.assign(rom) || !save_state_asks_->push(ask)) {
        ++tally_.savestate_refusals;
        return false;
    }
    return true;
}

bool SessionOwner::savestates_answered_(std::uint32_t gen) const noexcept {
    if (!save_state_facts_)
        return true;
    const std::uint32_t answered = save_state_facts_->sample().value.answered;
    return static_cast<std::int32_t>(answered - gen) >= 0;
}

bool SessionOwner::order_savestates(std::string_view image, std::uint32_t gen) {
    TASTY_SEAT_BODY(SessionOwner);
    const auto& conf = negotiated_.conf_str();
    if (!conf || !conf->savestate().has_value())
        return false;
    return ask_savestates_(image, gen);
}

std::uint32_t SessionOwner::savestates_answered() {
    TASTY_SEAT_BODY(SessionOwner);
    return save_state_facts_ ? save_state_facts_->sample().value.answered : 0u;
}

bool SessionOwner::arm_open_save(
    const ContentRequest& cr,
    UiRequest::SaveChoice choice,
    CorrelationTag tag
) {
    TASTY_SEAT_BODY(SessionOwner);
    const auto core = cores::find_core(negotiated_.core_name().view());
    if (core && (*core)->profile->staging.order != cores::StageOrder::None)
        return false;
    if (mounts_.save_mount_live() || mounts_.mount_held() || ladder_.live()) {
        ++tally_.open_saves_skipped;
        return false;
    }
    auto path = open_save_path_(cr.path.view(), choice);
    if (!path) {
        ++tally_.open_saves_skipped;
        return false;
    }
    mounts_.arm_load_save(
        MountAsk{
            .which = UiRequest::Kind::LoadFile,
            .index = proto::IoIndex{0},
            .path = *path,
            .tag = tag
        },
        cr
    );
    ++tally_.open_saves_armed;
    return true;
}

std::optional<PathText> SessionOwner::open_save_path_(
    std::string_view rom,
    UiRequest::SaveChoice choice
) {
    const auto& core_name = negotiated_.core_name();
    if (core_name.empty())
        return std::nullopt;
    const bool replay = choice != UiRequest::SaveChoice::User;
    if (replay && replay_save_root_.empty())
        return std::nullopt;
    std::string dir(replay ? replay_save_root_.view() : std::string_view{"saves"});
    dir += '/';
    dir.append(core_name.view());
    if (!vfs_.ensure_dir(dir))
        return std::nullopt;
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
    if (!out.assign(path))
        return std::nullopt;
    return out;
}

bool SessionOwner::prime_replay_save_(std::string_view path, bool seeded) {
    std::vector<std::uint8_t> seed;
    if (seeded) {
        std::string from(replay_save_root_.view());
        from += "/seed.sav";
        auto f = vfs_.open(from, svc::OpenMode::ReadWhole);
        const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
        if (!sz)
            return false;
        auto buf = read_whole(**f, sz->v);
        if (!buf)
            return false;
        seed = std::move(*buf);
    }
    return durable_write(vfs_, path, std::as_bytes(std::span<const std::uint8_t>(seed)))
        .has_value();
}

bool SessionOwner::rung_pending() const noexcept {
    return rung_wait_() == Wait::Now;
}

SessionOwner::Wait SessionOwner::rung_wait_() const noexcept {
    if (reboot_.owed())
        return Wait::None;
    return roster_.wait(*this);
}

infra::OptRef<Rung> SessionOwner::rung(RungSlot slot) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return rungs_.rung(slot);
}

infra::OptRef<const Rung> SessionOwner::rung(RungSlot slot) const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return rungs_.rung(slot);
}

void SessionOwner::step_held(RungSlot slot) {
    TASTY_SEAT_BODY(SessionOwner);
    switch (slot) {
        case RungSlot::BootLadder:
            step_ladder_();
            return;
        case RungSlot::SavestateHold:

            step_savestate_hold_();
            return;
        case RungSlot::SaveMount:
            mounts_.step_save_mount(*this);
            return;
        case RungSlot::AddonsAfter:

            if (!rungs_.addons && rungs_.addons_after && content_ordered_()) {
                rungs_.addons = std::move(rungs_.addons_after);
                rungs_.addons_after.reset();
            }
            return;
        case RungSlot::Watch:
            content_.step_watch();
            return;
        case RungSlot::Addons:
        case RungSlot::Pieces:
        case RungSlot::Load:
        case RungSlot::Manifest:
        case RungSlot::Walk:
        case RungSlot::Companion:
        case RungSlot::kCount:
            return;
    }
}

RungRoster::Wait SessionOwner::held_wait(RungSlot slot) const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    const auto each = [](bool live, bool now) noexcept {
        return !live ? Wait::None : (now ? Wait::Now : Wait::Later);
    };
    const bool room = inbox_.ring().size() < proto::kLinkTxCapacity;
    switch (slot) {
        case RungSlot::BootLadder:
            return each(ladder_.live(), (ladder_.moved() || rungs_.walk_answer_fresh) && room);
        case RungSlot::SavestateHold:
            return each(
                rungs_.ss_hold.has_value(),
                rungs_.ss_hold && savestates_answered_(rungs_.ss_hold->gen)
            );
        case RungSlot::SaveMount:
            return each(mounts_.save_mount_live(), false);
        case RungSlot::AddonsAfter:
            return each(!rungs_.addons && rungs_.addons_after.has_value(), content_ordered_());
        case RungSlot::Watch:
            return each(content_.content_watched(), content_.watch_answered() && room);
        case RungSlot::Addons:
        case RungSlot::Pieces:
        case RungSlot::Load:
        case RungSlot::Manifest:
        case RungSlot::Walk:
        case RungSlot::Companion:
        case RungSlot::kCount:
            break;
    }
    return Wait::None;
}

void SessionOwner::settle(RungSlot slot, Rung::Pass pass) {
    TASTY_SEAT_BODY(SessionOwner);
    switch (slot) {
        case RungSlot::Addons:
            return settle_addons_(pass);
        case RungSlot::Pieces:
            return settle_pieces_(pass);
        case RungSlot::Load:
            return settle_load_(pass);
        case RungSlot::Manifest:
            return settle_manifest_(pass);
        case RungSlot::Walk:
            return settle_walk_(pass);
        case RungSlot::Companion:
            if (pass == Rung::Pass::Done)
                rungs_.companion_walk.reset();
            return;
        case RungSlot::BootLadder:
        case RungSlot::SavestateHold:
        case RungSlot::SaveMount:
        case RungSlot::AddonsAfter:
        case RungSlot::Watch:
        case RungSlot::kCount:
            return;
    }
}

void SessionOwner::settle_addons_(Rung::Pass pass) {
    if (pass == Rung::Pass::Waiting)
        return;
    tally_.addons_sent += rungs_.addons->sent();
    tally_.addons_missing += rungs_.addons->missing();
    if (rungs_.addons->image_sent())
        ++tally_.ram_images_sent;
    if (rungs_.addons->image_failed()) {
        ++tally_.ram_images_declined;
        (void)owner_events_.push(
            infra::make<Event>(
                Event::RamImageDeclined{.why = Errc::io},
                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
            )
        );
    }
    rungs_.addons.reset();
    if (auto& picked = content_.picked_load()) {
        const PickedLoad load = *picked;
        picked.reset();
        run_load_(load);
    }
    return;
}

void SessionOwner::settle_pieces_(Rung::Pass pass) {
    if (pass == Rung::Pass::Done) {
        if (!rungs_.pieces->resumed()) {
            ++tally_.files_loaded;
            publish_done(content_.content_owed().which, content_.content_owed().tag);
        }
        content_.watch_last_piece();
    } else if (pass == Rung::Pass::Failed) {
        ++tally_.files_failed;
    } else {
        return;
    }
    rungs_.pieces.reset();
    return;
}

void SessionOwner::settle_load_(Rung::Pass pass) {
    if (pass != Rung::Pass::Done && pass != Rung::Pass::Failed)
        return;
    rungs_.load.reset();
    if (pass == Rung::Pass::Done) {
        ++tally_.files_loaded;
        windows_.opened();
        publish_done(content_.content_owed().which, content_.content_owed().tag);
    } else {
        ++tally_.files_failed;
    }
    windows_.publish();
    return;
}

void SessionOwner::settle_manifest_(Rung::Pass pass) {
    windows_.publish();
    if (pass == Rung::Pass::Failed) {
        const Errc why = rungs_.manifest->failure();
        rungs_.manifest.reset();
        give_up_start_(why);
        return;
    }
    if (pass != Rung::Pass::Done)
        return;

    if (!rungs_.manifest->error().empty()) {
        ++tally_.manifest_errors;
        (void)owner_events_.push(
            infra::make<Event>(
                Event::InfoRequest{.id = InfoId::RomLoadFailed},
                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
            )
        );
    }
    rungs_.manifest.reset();
    finish_core_made_();
    return;
}

void SessionOwner::settle_walk_(Rung::Pass pass) {
    if (pass == Rung::Pass::Done) {
        ++tally_.files_loaded;
        publish_done(content_.content_owed().which, content_.content_owed().tag);
    } else if (pass == Rung::Pass::Failed) {
        ++tally_.files_failed;
        if (rungs_.walk->refused() || rungs_.walk->stalled()) {
            (void)owner_events_.push(
                infra::make<Event>(
                    Event::InfoRequest{.id = InfoId::RomLoadFailed},
                    Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
                )
            );
        }
    } else {
        return;
    }
    if (rungs_.walk_gen != 0) {
        rungs_.walk_answer_fresh = true;
        rungs_.walk_answer = cores::MountStatus{
            .generation = rungs_.walk_gen,
            .state = pass == Rung::Pass::Done ? cores::MountState::Done : cores::MountState::Failed
        };
        rungs_.walk_gen = 0;
    }
    rungs_.walk.reset();
    return;
}

bool SessionOwner::order_walk(proto::IoIndex slot, std::string_view path, std::uint32_t gen) {
    TASTY_SEAT_BODY(SessionOwner);
    if (rungs_.any_live(kWalkBlockers))
        return false;
    content_.content_owed() = Owed{};
    rungs_.walk_answer = cores::MountStatus{.generation = gen, .state = cores::MountState::Failed};
    const auto row = cores::find_core(negotiated_.core_name().view());
    if (!row || (*row)->make_loader == nullptr)
        return true;
    const bool turbo = negotiated_.conf_str() && negotiated_.conf_str()->declares_turbo();
    auto loader = (*row)->make_loader(
        cores::LoaderContext{vfs_, *(*row)->profile, turbo, content_.loader_memo(), aperture_}
    );
    const cores::LoadAsk ask{.index = slot, .path = path, .disc = true};
    const auto plan = loader->plan(ask, status_word());
    auto w = plan ? LoadWalk::start(std::move(loader), *plan, (*row)->profile->windows)
                  : Ex<LoadWalk>{std::unexpected(plan.error())};
    if (!w) {
        ++tally_.files_failed;
        return true;
    }
    content_.forget_watch();
    rungs_.walk.emplace(std::move(*w));
    rungs_.walk_gen = gen;
    rungs_.walk_answer.state = cores::MountState::Pending;
    return true;
}

cores::MountStatus SessionOwner::walk_status() {
    TASTY_SEAT_BODY(SessionOwner);
    return rungs_.walk_answer;
}

void SessionOwner::begin_companion(std::string_view path) {
    TASTY_SEAT_BODY(SessionOwner);
    if (!companion_.present())
        return;
    if (rungs_.companion_walk) {
        ++tally_.companion_skips;
        return;
    }
    auto w = companion_.plan(path);
    if (!w) {
        ++tally_.companion_skips;
        return;
    }
    content_.forget_watch();
    rungs_.companion_walk.emplace(std::move(*w));
    ++tally_.companion_walks;

    if (!rungs_.companion_walk->loads())
        step_companion_();
    if (rungs_.companion_walk && !rungs_.companion_walk->loads())
        step_companion_();
}

void SessionOwner::after_content_(const ContentPick::After& after) {
    if (after.companion)
        begin_companion(after.companion->view());
}

void SessionOwner::step_companion_() {
    if (!rungs_.companion_walk)
        return;
    settle(RungSlot::Companion, rungs_.companion_walk->step(roster_.edges(), *this));
}

std::uint32_t SessionOwner::next_generation() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return act_keys_.next();
}

bool SessionOwner::order(const proto::LinkOp& op) {
    TASTY_SEAT_BODY(SessionOwner);
    return push_dropping(inbox_, tally_, op);
}

Ex<proto::FileId> SessionOwner::intern_bytes(
    std::span<const std::uint8_t> bytes,
    std::string_view ext,
    std::uint32_t load_addr
) {
    TASTY_SEAT_BODY(SessionOwner);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    auto id = inbox_.intern_file(std::move(buf), ext, std::string_view{}, load_addr);
    if (!id)
        return std::unexpected(id.error());
    return *id;
}

Ex<proto::FileId> SessionOwner::intern_piece(
    std::span<const std::uint8_t> bytes,
    std::string_view ext,
    std::uint64_t whole,
    std::uint64_t offset
) {
    TASTY_SEAT_BODY(SessionOwner);
    std::vector<std::uint8_t> buf(bytes.begin(), bytes.end());
    return inbox_.intern_file(std::move(buf), ext, std::string_view{}, 0, 0, whole, offset);
}

Ex<proto::FileId> SessionOwner::intern_path(std::string_view path, std::uint64_t size_bytes) {
    TASTY_SEAT_BODY(SessionOwner);
    auto id = inbox_.intern_file_path(path, size_bytes);
    if (!id)
        return std::unexpected(id.error());
    ++tally_.images_sized;
    return *id;
}

Ex<proto::FileId> SessionOwner::intern_whole(
    std::vector<std::uint8_t>&& bytes,
    std::string_view path
) {
    TASTY_SEAT_BODY(SessionOwner);
    const std::size_t dot = path.rfind('.');
    const std::string_view ext = dot == std::string_view::npos ? std::string_view{}
                                                               : path.substr(dot);
    return inbox_.intern_file(std::move(bytes), ext, path, 0);
}

std::optional<bool> SessionOwner::file_tx_answer(std::uint32_t act) {
    TASTY_SEAT_BODY(SessionOwner);
    const FileTxLevel level = ftx_level_cell_.sample().value;
    if (act == 0 || level.stream_act != act)
        return std::nullopt;
    return level.stream_served;
}

bool SessionOwner::order_load_facts(std::span<const std::uint8_t> facts) {
    TASTY_SEAT_BODY(SessionOwner);
    if (inbox_.ring().size() >= proto::kLinkTxCapacity)
        return false;
    const auto slab = inbox_.intern(facts);
    if (!slab)
        return false;
    return order(proto::LinkOp::LoadFacts{.facts = *slab});
}

std::size_t SessionOwner::link_in_flight() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return inbox_.ring().size();
}

std::size_t SessionOwner::link_word_bytes() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return negotiated_.link_wide() ? 2u : 1u;
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

void SessionOwner::bios_missing_() {

    (void)owner_events_.push(
        infra::make<Event>(
            Event::InfoRequest{.id = InfoId::CdBiosMissing},
            Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
        )
    );
}

bool SessionOwner::arm_loader(
    const ContentRequest& req,
    std::string_view save,
    std::uint64_t save_bytes
) {
    TASTY_SEAT_BODY(SessionOwner);
    return content_.arm_loader(req, save, save_bytes);
}

void SessionOwner::perform_content(const ContentRequest& req) {
    TASTY_SEAT_BODY(SessionOwner);
    after_content_(content_.perform_content(req));
}

void SessionOwner::launch_content(const ContentRequest& cr) {
    TASTY_SEAT_BODY(SessionOwner);
    after_content_(content_.launch_content(cr));
}

void SessionOwner::step_ladder_() {
    rungs_.walk_answer_fresh = false;
    const OwnerLadder::Step step = ladder_.step();
    if (step.bios_missing)
        bios_missing_();
    if (step.ended)
        finish_ladder_(*step.ended);
}

void SessionOwner::finish_ladder_(const OwnerLadder::Ended& end) {
    mounts_.ladder_ended();
    const Event::Head head{EmitSite{ERR_SITE()}, {}, kUncaused};
    if (end.ok) {
        if (end.touched)
            (void)owner_events_.push(infra::make<Event>(Event::SdActivity{.slot = end.slot}, head));
        publish_done(end.owed.which, end.owed.tag);
    } else {
        (void)owner_events_.push(
            infra::make<Event>(Event::InfoRequest{.id = InfoId::ImageMountFailed}, head)
        );
    }

    if (reset_owed_.has_value()) {
        const proto::LinkOp::CoreReset op = *reset_owed_;
        reset_owed_.reset();
        (void)order_reset_(op, kUncaused);
    }
    if (boot_restore_ == BootRestore::Laddered) {
        finish_boot_restore_();
        return;
    }
    mounts_.arm_next_mount(*this);
}

void SessionOwner::drop_ladder_() {
    ladder_.drop();
    mounts_.drop();
    rungs_.pieces.reset();
    content_.drop_watch();
    rungs_.addons.reset();
    content_.picked_load().reset();
    rungs_.ss_hold.reset();
    rungs_.addons_after.reset();

    rungs_.manifest.reset();
    made_.reset();
    if (rungs_.load) {
        rungs_.load.reset();
        windows_.cancelled();
        windows_.publish();
        (void)owner_events_.push(
            infra::make<Event>(
                Event::ProgressUpdate{},
                Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
            )
        );
    }
    rungs_.walk_gen = 0;
    if (rungs_.walk) {
        rungs_.walk.reset();
        windows_.cancelled();
        windows_.publish();
    }
    reset_owed_.reset();

    if (boot_restore_ == BootRestore::Laddered)
        boot_restore_ = BootRestore::Asked;
    negotiated_.clear_core_name();
}

void SessionOwner::forget_core_() noexcept {
    rungs_.companion_walk.reset();
    companion_.withdraw();
    negotiated_.forget();
}

std::string_view SessionOwner::make_core_rel_() const noexcept {
    const UiRequest::LoadCore& pending = switch_.pending();
    if (pending.xml == XmlKind::Mra && !pending.path.empty())
        return pending.path.view();
    return negotiated_.defmra_rel();
}

bool SessionOwner::build_has_manifest_(const proto::ConfStr& cs) const {
    TASTY_SEAT_BODY(SessionOwner);
    const UiRequest::LoadCore& pending = switch_.pending();
    if (pending.xml == XmlKind::Mra && !pending.path.empty())
        return true;
    return !negotiated_.defmra_elected(cs.default_manifest(), pending).empty()
        || !boot_companion_.view().empty();
}

void SessionOwner::order_make_core_(bool fabric) {
    rungs_.manifest.reset();
    const std::string_view rel = make_core_rel_();

    const std::string built_from{rel.empty() ? boot_companion_.view() : rel};
    boot_companion_ = PathText{};
    negotiated_.note_build(built_from);
    const auto& conf = negotiated_.conf_str();
    const proto::LinkOp::AdoptCore op = builder_.build(
        CoreBuilder::Ask{
            .name = negotiated_.core_name().view(),
            .manifest = {.path = built_from, .text = negotiated_.manifest_text()},
            .irqs = conf ? conf->irq_bindings() : std::span<const proto::IrqBinding>{},
            .lw = conf ? conf->lw_windows() : std::span<const proto::AxiLwDecl>{},
            .fabric = fabric,
            .fabric_gen = fabric_gen_,
            .mount_slots = conf ? conf->mount_slot_mask() : std::uint16_t{0}
        }
    );

    if (!push_gating(inbox_, tally_, ops_.sleeper, infra::make<proto::LinkOp>(op))) {
        builder_.forget(op);
        give_up_start_(Errc::would_block);
        return;
    }

    if (negotiated_.made_with_manifest() && conf && conf->savestate().has_value()) {
        (void)ask_savestates_(negotiated_.manifest_rel().view(), next_generation());
    }

    start_watch_.rebound();
}

void SessionOwner::latch_boot_companion(std::string_view xml) noexcept {
    boot_companion_ = PathText{};
    if (svc::Vfs::extension_matches(xml, "MRA"))
        (void)boot_companion_.assign(xml);
}

void SessionOwner::order_hold_reset_() noexcept {
    order_(proto::LinkOp::HoldReset{});
}

template <class A>
void SessionOwner::order_(const A& alt) noexcept {
    (void)push_dropping(inbox_, tally_, alt);
}

template <class A>
bool SessionOwner::order_refusable_(const A& alt) noexcept {
    return push_refusing(inbox_, tally_, alt);
}

void SessionOwner::order_bind_decoders_(
    proto::LinkOp::DecoderTable table,
    std::uint32_t gen
) noexcept {
    (void)order_refusable_(proto::LinkOp::BindDecoders{.table = table, .gen = as_generation(gen)});
}

void SessionOwner::publish_done(UiRequest::Kind which, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    if (tag == kUncaused)
        return;
    (void)owner_events_.push(
        infra::make<Event>(
            Event::RequestDone{.which = which},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

void SessionOwner::publish_refusal(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    (void)owner_events_.push(
        infra::make<Event>(
            Event::RequestRefused{.which = which, .why = code},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

void SessionOwner::abort_start_(Errc why) noexcept {
    switch_.end(proto::LinkOp::AbortSwitch{.why = why, .gen = as_generation(switch_.gen())});
    order_bind_decoders_(proto::LinkOp::DecoderTable::Census, switch_.gen());
}

void SessionOwner::refuse_started_switch_(Errc why) noexcept {

    start_watch_.begin_refusal();
    (void)owner_events_.push(
        infra::make<Event>(
            Event::SessionFailed{.why = why},
            Event::Head{EmitSite{ERR_SITE()}, {}, switch_.pending_tag()}
        )
    );
    abort_start_(why);

    start_watch_.owe_front_end(switch_.pending());
}

void SessionOwner::expect_boot_negotiate() noexcept {
    switch_.latch_pending(UiRequest::LoadCore{}, kUncaused);
    switch_.latch_step_arg(CoreSwitch::StepArg{.loaded = true});
    start_watch_.expect_boot_negotiate();
}

void SessionOwner::on_slot_names_(const proto::LinkEvent::SaveBytes& e) {

    const SlotAsker who = e.tag               ? SlotAsker::Load
        : boot_restore_ == BootRestore::Asked ? SlotAsker::Boot
                                              : SlotAsker::Refresh;
    if (e.status != proto::SaveStatus::Bytes) {
        refuse_slot_names_(who, e.tag);
        return;
    }

    const auto path = rx_.bytes(e.rel_path);
    preview_slot_names_(rx_.bytes(e.payload));
    if (who == SlotAsker::Refresh)
        return;
    bind_slot_answer_(who == SlotAsker::Boot, e.tag, infra::as_text(path));
}

void SessionOwner::refuse_slot_names_(SlotAsker who, CorrelationTag tag) {
    if (who == SlotAsker::Load) {
        publish_refusal(UiRequest::Kind::LoadCoreConfig, Errc::not_found, tag);
    }
    if (who == SlotAsker::Boot)
        finish_boot_restore_();
}

void SessionOwner::preview_slot_names_(std::span<const std::uint8_t> pack) {
    if (!pack.empty()) {
        const std::span<const std::byte> previews = infra::byte_cast<const std::byte>(pack);
        saves_.order_slot_previews(previews.data(), previews.size());
    }
}

void SessionOwner::bind_slot_answer_(bool boot, CorrelationTag tag, std::string_view path) {
    const auto row = cores::find_core(negotiated_.core_name().view());
    const bool boots = row && (*row)->profile->reset.restore_boots;

    if (boots && ladder_.live()) {
        if (tag)
            publish_refusal(UiRequest::Kind::LoadCoreConfig, Errc::would_block, tag);
        if (boot)
            finish_boot_restore_();
        return;
    }
    const std::uint8_t slot = boot ? 0 : saves_.load_slot_slot();
    const bool laddered = saves_.bind_slot_config(slot, path) && boots && arm_restore_ladder_(path);

    if (boot && laddered) {
        boot_restore_ = BootRestore::Laddered;
        return;
    }
    if (boot)
        finish_boot_restore_();
}

bool SessionOwner::arm_restore_ladder_(std::string_view config) {
    const auto row = cores::find_core(negotiated_.core_name().view());
    if (!row)
        return false;
    return ladder_.arm_core(std::nullopt, config, **row, *this, Owed{});
}

void SessionOwner::finish_boot_restore_() noexcept {
    boot_restore_ = BootRestore::None;
    order_session_up_();
}

void SessionOwner::on(const proto::LinkEvent::SaveBytes& e) {
    if (e.status == proto::SaveStatus::Nothing)
        return;
    if (e.which == proto::SaveKind::SlotNames) {
        on_slot_names_(e);
        return;
    }
    saves_.on_save_bytes(e);
}

void SessionOwner::on(const proto::LinkEvent::ReadyEdge&) noexcept {
    ++tally_.link_event_unhandled;
}

void SessionOwner::on(const proto::LinkEvent::BlockRequest&) noexcept {
    ++tally_.link_event_unhandled;
}

void SessionOwner::on(const proto::LinkEvent::StartRefused& e) noexcept {

    if (e.bind_gen != as_generation(switch_.gen())) {
        ++tally_.stale_events;
        return;
    }

    if (start_watch_.ended_by_bridge(switch_.gen())) {
        abort_start_(e.err);
        return;
    }
    if (start_watch_.refused()) {
        ++tally_.stale_events;
        return;
    }
    refuse_started_switch_(e.err);
}

void SessionOwner::misrouted(const proto::LinkEvent&) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++tally_.link_event_misrouted;
}

static_assert(LinkEventMainSink<SessionOwner>);

void SessionOwner::on(const proto::LinkEvent::CoreMade& m) noexcept {
    if (m.bind_gen != switch_.gen()) {
        ++tally_.stale_events;
        return;
    }
    if (start_watch_.refused()) {
        ++tally_.stale_events;
        return;
    }
    start_watch_.end_bound();
    boot_restore_ = BootRestore::None;
    if (m.made)
        core_made_once_ = true;
    negotiated_.note_made(m.made);
    if (m.made) {
        rungs_.companion_walk.reset();
        companion_.make();
    }
    if (!m.made) {

        ++tally_.core_make_refusals;
        refuse_started_switch_(m.err);
        return;
    }
    made_ = MadeFacts{.restore_owed = m.restore_owed, .cheat_records = m.cheat_records};

    if (arm_manifest_())
        return;
    finish_core_made_();
}

bool SessionOwner::arm_manifest_() {
    rungs_.manifest.reset();
    if (!negotiated_.made_with_manifest())
        return false;
    const auto role = cores::manifest_doc_role();
    if (!role)
        return false;
    std::unique_ptr<cores::IManifestStager> stager = role->stager_of(
        negotiated_.manifest_text(),
        negotiated_.manifest_rel().view(),
        vfs_
    );
    if (stager == nullptr)
        return false;
    rungs_.manifest.emplace(std::move(stager), negotiated_.manifest_rel());
    return true;
}

void SessionOwner::finish_core_made_() noexcept {

    static constexpr CheatCatalog kNoCheats{};
    const MadeFacts made = std::exchange(made_, std::nullopt).value_or(MadeFacts{});
    cheat_catalog_cell_.publish(made.cheat_records ? negotiated_.cheats() : kNoCheats);
    if (made.restore_owed) {

        boot_restore_ = BootRestore::Asked;
        if (saves_.order_save_ask(proto::SaveKind::SlotNames, 0, CorrelationTag{}, true))
            return;
        boot_restore_ = BootRestore::None;
    }
    order_session_up_();
}

void SessionOwner::order_session_up_() noexcept {
    const std::vector<RememberedFile> remembered = read_remembered_files_();

    const bool index0_taken = std::ranges::any_of(remembered, [](const RememberedFile& f) noexcept {
        return f.row.ioctl_index == 0;
    });

    builder_.raise_osd();

    (void)push_gating(
        inbox_,
        tally_,
        ops_.sleeper,
        infra::make<proto::LinkOp>(proto::LinkOp::SessionUp{})
    );

    builder_.raise_sector();

    builder_.raise_sys();

    const bool ladder = ladder_.arm_start(negotiated_.core_name(), index0_taken, *this);
    const std::string rbf = start_rbf_();

    const bool slot0_held = mounts_.read_remembered_mounts(remembered_files_);
    const StartChain chain = StartChain::plan(
        start_facts_(rbf, index0_taken, slot0_held, !remembered.empty(), ladder),
        vfs_
    );

    const bool held = chain.holds_reset() && bind_.reset_level(true);
    order_remembered_files_(remembered);
    order_start_chain_(chain);
    if (held)
        (void)bind_.reset_level(false);

    mounts_.queue_boot_images(chain);
    mounts_.arm_next_mount(*this);
}

std::vector<SessionOwner::RememberedFile> SessionOwner::read_remembered_files_() const {
    std::vector<RememberedFile> out;
    const auto& conf = negotiated_.conf_str();
    const RememberedStem& stem = negotiated_.remembered_stem();
    if (!conf || stem.empty())
        return out;
    if (remembered_files_ == RememberedFiles::Ignore)
        return out;
    for (const proto::ConfStrFileRow& row : conf->remembered_rows()) {
        std::string path = read_remembered_path(vfs_, stem, RememberedSlot::File, row.ioctl_index);
        if (!path.empty())
            out.push_back(RememberedFile{row, std::move(path)});
    }
    return out;
}

void SessionOwner::order_remembered_files_(std::span<const RememberedFile> files) {
    for (const RememberedFile& f : files) {
        auto cr = content_.content_for(f.row, f.path);
        if (!cr) {
            ++tally_.remembered_missed;
            continue;
        }
        cr->whole = true;

        if (f.row.opensave && !vfs_.open(cr->path.view(), svc::OpenMode::Read)) {
            ++tally_.remembered_missed;
            continue;
        }
        const std::uint32_t failed = tally_.files_failed;
        const std::uint32_t drops = tally_.op_drops;
        (void)attach_row_savestates(f.row.opensave, cr->path.view());
        after_content_(content_.order_row_content(
            *cr,
            f.row.opensave,
            UiRequest::SaveChoice::User,
            kUncaused,
            *this
        ));
        if (tally_.files_failed != failed) {
            ++tally_.remembered_missed;
        } else if (tally_.op_drops == drops) {
            ++tally_.remembered_ordered;
        }
    }
}

StartChain::Facts SessionOwner::start_facts_(
    std::string_view rbf,
    bool index0_taken,
    bool slot0_held,
    bool remembered,
    bool ladder
) const {
    const std::string_view home = negotiated_.core_name().view();
    const cores::CoreProfile& p = cores::profile_for(home);
    return StartChain::Facts{
        .rule = p.start,
        .declared = p.start_assets,
        .home = home,
        .rbf = rbf,
        .manifest_load = negotiated_.made_with_manifest(),
        .generic_identity = negotiated_.generic_identity(),
        .index0_taken = index0_taken,
        .remembered = remembered,
        .ladder = ladder,
        .slot0_held = slot0_held
    };
}

std::string SessionOwner::start_rbf_() const {
    const UiRequest::LoadCore& pending = switch_.pending();
    if (pending.path.empty() || pending.xml != XmlKind::Rbf)
        return {};
    auto rel = resolve_bitstream(vfs_, pending.path.view(), pending.xml);
    return rel ? std::move(*rel) : std::string{};
}

void SessionOwner::order_start_chain_(const StartChain& chain) {
    const auto files = chain.files();
    for (std::size_t i = 0; i < files.size(); ++i) {
        const StartFile& f = files[i];
        const std::uint32_t failed = tally_.files_failed;
        const std::uint32_t drops = tally_.op_drops;
        if (f.offset != 0) {
            order_rbf_tail_(f.path.view(), f.dest, f.offset);
        } else {
            ContentRequest cr{};
            cr.path = f.path;
            cr.slot = static_cast<std::uint8_t>(f.dest.v);
            cr.whole = chain.holds_reset();

            cr.edge = i + 1 == files.size() ? ContentEdge::ChainEnd : ContentEdge::Silent;
            after_content_(content_.perform_content(cr));
        }
        if (tally_.files_failed == failed && tally_.op_drops == drops)
            ++tally_.start_assets_ordered;
    }
}

void SessionOwner::order_rbf_tail_(
    std::string_view path,
    proto::WideIoIndex dest,
    std::uint64_t offset
) {
    auto src = stream_open(vfs_, path, svc::OpenMode::ReadWhole);
    if (!src || src->size < offset) {
        ++tally_.files_failed;
        return;
    }
    if (src->size == offset) {
        ++tally_.files_failed;
        return;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(src->size - offset));
    if (!stream_read(*src->f, offset, std::as_writable_bytes(std::span<std::uint8_t>(buf)))) {
        ++tally_.files_failed;
        return;
    }
    const std::size_t dot = path.rfind('.');
    const std::string_view ext = dot == std::string_view::npos ? std::string_view{}
                                                               : path.substr(dot);
    LoadKey key(buf.size());
    key.feed(buf);
    const auto id = inbox_.intern_file(std::move(buf), ext, path, 0, key.value());
    if (!id) {
        ++tally_.files_failed;
        return;
    }
    (void)inbox_.stamp_edge(*id, ContentEdge::Silent);

    if (!content_.order_content_tx(
            *id,
            ContentPick::ContentWatch{
                .total = src->size - offset,
                .crc = key.value(),
                .wire_index = static_cast<std::uint8_t>(dest.v),
                .cut_on_hold = true
            }
        )) {
        ++tally_.op_drops;
        return;
    }
    ++tally_.files_loaded;
}

void SessionOwner::on(const proto::LinkEvent::ConfStr& c) {
    if (c.bind_gen.v != switch_.gen()) {
        ++tally_.stale_events;
        return;
    }

    if (start_watch_.refused()) {
        ++tally_.stale_events;
        return;
    }

    if (c.read == proto::LinkEvent::ConfStrRead::Refused) {
        if (!start_watch_.awaiting_conf_str()) {
            ++tally_.link_event_unhandled;
            return;
        }
        refuse_started_switch_(c.err);
        return;
    }
    const bool awaited = start_watch_.take_conf_str();
    const auto accepted = negotiated_.accept(c, aperture_);
    if (!accepted) {

        if (awaited) {
            refuse_started_switch_(accepted.error().code);
        } else {
            ++tally_.link_event_unhandled;
        }
        return;
    }
    const proto::ConfStr& cs = accepted->get();
    const std::string_view name = cs.name();
    bind_.identity(cs);
    const std::span<const proto::AxiLwDecl> lw = cs.lw_windows();

    const bool manifest = build_has_manifest_(cs);
    const bool fabric = CoreBuilder::fabric_route(name, manifest);
    const std::optional<std::uint32_t> owned = fabric ? builder_.claim_sector(
                                                            {.name = name,
                                                             .manifest = manifest,
                                                             .irqs = cs.irq_bindings(),
                                                             .lw = lw,
                                                             .fabric_gen = fabric_gen_}
                                                        )
                                                      : std::nullopt;
    bind_.doorbells(cs, lw, owned);
    bind_.joy(name);

    proto::LinkOp::BindConfig op{};
    CoreIni::Resolved ini = ini_.resolve(name, negotiated_.pending_facts());
    const svc::ConfigSnapshot& cfg = ini.cfg;
    if (ini.waitmount_exhausted)
        op.waitmount_exhausted = true;
    if (ini.launcher)
        launcher_.latch(*ini.launcher, name, switch_.pending().path.view());
    op.conf = conf_switches(cfg);
    ini_.publish(cfg);
    launcher_.publish(name, svc::direct_video_resolved(cfg) != 0, switch_.pending().path.view());
    if (cs.savestate().has_value() && !name.empty()) {
        (void)vfs_.ensure_dir(std::string("savestates/") + std::string(name));
    }

    op.cfg = negotiated_.key_names(cs, switch_.pending());
    op.savestate_dir_ready = cs.savestate().has_value();
    (void)push_gating(inbox_, tally_, ops_.sleeper, infra::make<proto::LinkOp>(op));
    bind_.facts(negotiated_.pending_facts());
    bind_.uart(effective_name(name, negotiated_.pending_facts()), &cs);

    order_make_core_(fabric);
}

void SessionOwner::arm_replay_ini(bool strict) noexcept {
    ini_.arm_replay(strict);
}

bool SessionOwner::launcher_swap_held_() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return start_watch_.before_seats() || switch_.switch_standing() || readiness_.recovering();
}

bool SessionOwner::launcher_swap_room_() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return proto::kLinkTxCapacity - inbox_.ring().size() >= CoreSwitch::kTeardownOps
        && park_.ask_room();
}

void SessionOwner::on(const proto::LinkEvent::IdentityMismatch& w) noexcept {
    ++tally_.identities_fail;
    if (w.bind_gen.v != switch_.gen())
        return;
    if (start_watch_.accept_refusal(w.at_accept)) {

        if (w.err == Errc::fpga_not_ready && core_made_once_) {
            start_watch_.wedged();
            ++tally_.fabric_wedges;

            order_bind_decoders_(proto::LinkOp::DecoderTable::Census, switch_.gen());
            reboot_.settle(true);
            return;
        }
        refuse_started_switch_(w.err);
        return;
    }
    if (start_watch_.identity_budget_spent())
        give_up_start_(w.err);
}

void SessionOwner::on(const proto::LinkEvent::IdentityMatched& w) noexcept {
    ++tally_.identities_ok;
    if (w.bind_gen.v != switch_.gen())
        return;
    negotiated_.note_identity(w);

    if (!start_watch_.take_identity()) {
        negotiated_.note_width(w.wide);
        return;
    }

    const BindOutcome bind = bind_link_rows(kLinkRows, inbox_, switch_.gen());
    tally_.order_refusals += bind.decoders_refused;
    tally_.op_drops += bind.slots_refused;
    if (negotiated_.facts_pending()) {
        bind_.facts(negotiated_.pending_facts());
        negotiated_.facts_sent();
    }
    switch_.end(
        proto::LinkOp::ApplyCore{
            .loaded = switch_.pending_step_arg().loaded,
            .gen = as_generation(switch_.gen())
        }
    );
    start_watch_.await_conf_str();
}

void SessionOwner::switch_minted(const UiRequest::LoadCore& req) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    negotiated_.forget_identity();
    readiness_.clear_owed();
    start_watch_.supersede(req);

    readiness_.disarm();
    launcher_.withdraw();
    note_memsz_(req);
}

void SessionOwner::drop_core_acts() {
    TASTY_SEAT_BODY(SessionOwner);
    drop_ladder_();
    content_.loader_memo() = cores::LoaderMemo{};
    start_watch_.drop_conf_str_wait();
}

void SessionOwner::forget_core() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    boot_companion_ = PathText{};
    forget_core_();
}

void SessionOwner::release_and_await_identity_(std::uint32_t gen) noexcept {
    order_(proto::LinkOp::ReleaseReset{});
    order_bind_decoders_(proto::LinkOp::DecoderTable::PreSession, gen);
    start_watch_.await_identity();
    negotiated_.note_width(false);
}

void SessionOwner::programmed(bool ok) {
    TASTY_SEAT_BODY(SessionOwner);
    readiness_.latch_load_ok(ok);
    ++fabric_gen_;
    if (ok)
        negotiated_.latch_mra_facts(switch_.pending());
}

void SessionOwner::retire_lw_at_exit() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    if (builder_.sector_mapped())
        (void)builder_.retire_sector(readiness_.fabric_configured());
    if (builder_.osd_mapped())
        (void)builder_.retire_osd(readiness_.fabric_configured());
    if (builder_.sys_mapped())
        (void)builder_.retire_sys();
}

void SessionOwner::fabric_suspect() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    builder_.abandon_sector();
    builder_.abandon_osd();
    builder_.abandon_sys();
}

bool SessionOwner::ask_recovery_switch() {
    TASTY_SEAT_BODY(SessionOwner);
    switch_.ask_or_refuse(launcher_.alias(switch_.pending()), switch_.pending_tag(), *this);
    return switch_.switch_standing();
}

void SessionOwner::parkless_teardown() {
    TASTY_SEAT_BODY(SessionOwner);
    drop_ladder_();
    forget_core_();
    switch_.order_teardown();
}

bool SessionOwner::program_owed() const noexcept {
    return switch_.program_owed();
}

int SessionOwner::wake_hint_ms(int floor_ms) const noexcept {
    const auto until = [this](const os::Deadline& d, int floor) noexcept {
        const std::int64_t left_ns = (d.at() - clock_.now()).count();
        if (left_ns <= 0)
            return 0;
        const std::int64_t ms = (left_ns + 999'999) / 1'000'000;
        return ms < floor ? static_cast<int>(ms) : floor;
    };

    if (const auto& owed = reboot_.owed()) {
        const int f = owed->ordered ? floor_ms : std::min(floor_ms, kLastOrderRetryMs);
        return until(reboot_.due(), f);
    }
    const bool owed = switch_.load_held()
        || (start_watch_.front_end_owed() && !start_watch_.before_seats());
    const bool ask_due = owed && !switch_.switch_standing() && !readiness_.recovering();
    if (switch_.last_order_owed() || ask_due)
        return floor_ms < kLastOrderRetryMs ? floor_ms : kLastOrderRetryMs;

    if (rung_wait_() != Wait::None && floor_ms > kPieceRetryMs)
        floor_ms = kPieceRetryMs;
    if (const auto due = start_watch_.bound_deadline())
        floor_ms = until(*due, floor_ms);
    if (!switch_.receipt_held() || readiness_.recovering())
        return floor_ms;

    if (switch_.lease_holding())
        return std::min(floor_ms, kLastOrderRetryMs);
    return until(switch_.pause_due(), floor_ms);
}

void SessionOwner::on(const proto::LinkEvent::RebootQuiesced& e) {

    const auto& owed = reboot_.owed();
    if (!owed || !owed->ordered || e.seq != owed->seq) {
        ++tally_.stale_events;
        return;
    }
    reboot_.finish();
}

}
