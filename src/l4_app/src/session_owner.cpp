// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_owner.h"

#include <fcntl.h>

#include <chrono>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
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
#include "proto/rtc_image.h"
#include "svc/file.h"
#include "svc/search_policy.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

[[nodiscard]] proto::BindGeneration as_generation(std::uint32_t gen) noexcept {
    return proto::BindGeneration{static_cast<std::uint16_t>(gen)};
}

[[nodiscard]] phase::Facts facts_of(
    const CoreSwitch& sw,
    const NegotiatedCore& nc,
    const BoardReboot& reboot,
    Errc why
) noexcept {
    const auto& owed = reboot.owed();
    return phase::Facts{
        .gen = phase::Gen{sw.gen()},
        .loaded = sw.pending_step_arg().loaded,
        .conf_gen = nc.conf_scope().gen,
        .why = why,
        .cold = owed ? owed->cold : true
    };
}

}

constexpr int kLastOrderRetryMs = 1;

constexpr int kPieceRetryMs = 5;

static_assert(
    cores::PayloadPieces::kAhead + InternedFile::kHeldOps + 1 < InternedFile::kMaxLiveFiles
);

SessionOwner::SessionOwner(SwitchPark& park, const Link& link) noexcept
    : park_(park),
      inbox_(link.inbox),
      rx_(link.rx),
      identity_record_cell_(link.identity_record_cell),
      ini_(link.config_cell, link.storage, link.ops.sleeper),
      owner_events_(link.owner_events.producer()),
      cheat_catalog_cell_(link.cheat_catalog_cell),
      ui_requests_(link.main_wake),
      status_cell_(link.status_cell),
      mount_status_cell_(link.mount_status_cell),
      save_extent_cell_(link.save_extent_cell),
      clock_(
          link.clock.has_value() ? static_cast<const os::IClock&>(*link.clock)
                                 : static_cast<const os::IClock&>(default_clock_)
      ),
      programmer_(link.programmer),
      readiness_(link.pin_levels, programmer_),
      vfs_(link.storage),
      ladder_(inbox_, link.ladder_cell, tally_, vfs_, clock_),
      negotiated_(rx_, link.conf_str_cell, vfs_, inbox_),
      mounts_(
          MountQueue::Wiring{
              .vfs = vfs_,
              .inbox = inbox_,
              .events = link.owner_events,
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
              .owner_events = link.owner_events,
              .vfs = vfs_,
              .status_cell = status_cell_,
              .negotiated = negotiated_,
              .sleeper = ops_.sleeper
          }
      ),
      companion_(vfs_, negotiated_, tally_, aperture_.phys.v, link.companion_binds),
      window_map_(link.window_map),
      ftx_level_cell_(link.file_tx_level_cell),
      belt_(
          RungEdges{
              .inbox = inbox_,
              .level = ftx_level_cell_,
              .events = owner_events_,
              .window_map = window_map_,
              .aperture = aperture_,
              .vfs = vfs_,
              .windows = windows_,
              .clock = clock_
          },
          tally_
      ),
      content_(
          ContentPick::Wiring{
              .inbox = inbox_,
              .ftx_level_cell = ftx_level_cell_,
              .events = link.owner_events,
              .vfs = vfs_,
              .window_map = window_map_,
              .negotiated = negotiated_,
              .mounts = mounts_,
              .ladder = ladder_,
              .belt = belt_,
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
      ladder_host_(
          LadderHostAdapter::Edges{
              .inbox = inbox_,
              .tally = tally_,
              .act_keys = act_keys_,
              .ftx_level_cell = ftx_level_cell_,
              .negotiated = negotiated_,
              .status_cell = status_cell_,
              .mount_status_cell = mount_status_cell_,
              .save_extent_cell = save_extent_cell_,
              .save_state_facts = save_state_facts_,
              .belt = belt_
          },
          *this
      ),
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
              .events = link.owner_events,
              .owner_tally = tally_
          }
      ) {
    (void)front_end_image_.assign(kFrontEndImage);
    handoff_ = link.boot_handoff;
    if (link.log_lane)
        log_.emplace(link.log_lane->producer());
    launcher_.bind(link.launcher_demand, link.launcher_wake);
    publish_fabric_();
}

void SessionOwner::set_identity_poll_budget(std::uint32_t n) noexcept {
    identity_poll_budget_ = n;
}

void SessionOwner::set_start_bound_ms(std::uint32_t ms) noexcept {
    start_bound_ms_ = ms;
}

void SessionOwner::set_readiness_poll_budget(std::uint32_t n) noexcept {
    readiness_.set_readiness_poll_budget(n);
}

void SessionOwner::set_replay_save_root(std::string_view rel) noexcept {
    (void)replay_save_root_.assign(rel);
}

void SessionOwner::grant_pause(const hal::ThreadRole& row, xthread::PauseLatch& latch) noexcept {
    switch_.grant_pause(row, latch);
}

void SessionOwner::on_seats_live() noexcept {
    before_seats_ = false;
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
    return fallback_cell_;
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
    unsigned acks = 0;
    for (const MainPassStep& s : kMainPassSteps) {
        const std::optional<unsigned> took = run_(s.step);
        if (!took)
            break;
        acks += *took;
    }
    return acks;
}

std::optional<unsigned> SessionOwner::run_(MainPassStep::Step step) {
    using S = MainPassStep::Step;
    switch (step) {
        case S::Reap:
            builder_.reap();
            return 0u;
        case S::Reboot:
            if (!reboot_.watch())
                return 0u;
            publish_fabric_();
            return std::nullopt;
        case S::LastOrder:
            (void)switch_.push_last_order();
            return 0u;
        case S::HeldLoad:
            switch_.retry_held_load(*this);
            return 0u;
        case S::Belt:
            belt_.step(*this, ladder_host_);
            return 0u;
        case S::ParkAcks:
            return switch_.take_park_acks();
        case S::Program:
            program_();
            return 0u;
        case S::StartBound:
            watch_start_();
            return 0u;
        case S::Bridge:
            watch_bridge_();
            return 0u;
        case S::FrontEnd:
            fall_back_to_front_end_();
            return 0u;
        case S::LauncherSwap:
            swap_launcher_();
            return 0u;
        case S::Readiness:
            watch_readiness_();
            return 0u;
        case S::Fabric:
            publish_fabric_();
            return 0u;
        case S::kCount:
            break;
    }
    std::unreachable();
}

void SessionOwner::program_() {
    if (rebooting_() && switch_.switch_standing() && !switch_.last_order_owed())
        end_rebooting_switch_();
    const bool receipt_held = switch_.receipt_held();
    const std::uint32_t expiries = switch_.tally().pause_expiries;
    const auto programmed_gen = switch_.try_program(*this);

    if (receipt_held && !switch_.receipt_held() && !rebooting_()) {
        phase_edge_(
            switch_.tally().pause_expiries != expiries ? phase::Stimulus::PauseExpired
                                                       : phase::Stimulus::ParkComplete
        );

        phase::Facts f = facts_of(switch_, negotiated_, reboot_, Errc::negotiation);
        f.bound = os::Deadline::in(clock_, std::chrono::milliseconds{start_bound_ms_});
        const phase::Stimulus on = programmed_gen ? phase::Stimulus::ProgramOk
                                                  : phase::Stimulus::ProgramFailed;
        judge_(phase_.take(on, f), on);
    }
    if (programmed_gen)
        release_and_await_identity_(*programmed_gen);
}

void SessionOwner::swap_launcher_() {
    const auto req = launcher_.swap_to_image(launcher_swap_held_(), launcher_swap_room_());
    if (!req)
        return;
    if (rebooting_()) {
        refuse_rebooting_(phase::Stimulus::LoadCore);
    } else {
        (void)switch_.ask(*req, kUncaused, *this);
    }
}

void SessionOwner::watch_readiness_() {
    const ReadinessWatch::SwitchView sw{
        .standing = switch_.switch_standing(),
        .programmed = switch_.switch_programmed()
    };
    const auto reboot = readiness_.watch_readiness(sw, *this);
    if (reboot)
        reboot_.settle(reboot->cold);
    phase_after_watch_(reboot.has_value());
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
    if (rebooting_())
        refuse_rebooting_(phase::Stimulus::LoadCore);
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
        owner_events_.push(
            Event::RamImageDeclined{.why = Errc::bad_format},
            Event::Head{EmitSite{ERR_SITE()}, {}, head.tag}
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
        if (!ladder_.arm_core(req.edge, {}, **row, ladder_host_, Owed{kKind, head.tag}))
            publish_refusal(kKind, Errc::core_load, head.tag);
        return;
    }
    if (order_reset_(op, head.tag) && clears)
        ladder_.clear_noreset();
}

bool SessionOwner::order_reset_(const proto::LinkOp::CoreReset& op, CorrelationTag tag) noexcept {
    if (!ladder_host_.order(op)) {
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
    phase_edge_(phase::Stimulus::Reboot);
}

void SessionOwner::misrouted(const UiRequest&) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++tally_.ui_request_misrouted;
}

bool SessionOwner::refuse_in_load_(UiRequest::Kind kind, CorrelationTag tag) noexcept {
    const bool rom_waits = mounts_.save_holds_load();
    if (!belt_.any_live(kLoadBelt) && !rom_waits)
        return false;
    refuse_busy(kind, tag);
    return true;
}

void SessionOwner::refuse_busy(UiRequest::Kind asked, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    ++tally_.loads_refused_busy;
    publish_refusal(asked, Errc::would_block, tag);
}

void SessionOwner::watch_start_() noexcept {

    const auto due = phase_.bound();
    if (start_bound_ms_ == 0 || !due || !due->expired(clock_))
        return;
    if (rebooting_()) {
        phase_.end_bound();
        return refuse_rebooting_(phase::Stimulus::BoundExpired);
    }
    give_up_start_(Errc::timeout, phase::Stimulus::BoundExpired);
}

void SessionOwner::watch_bridge_() noexcept {

    if (switch_.switch_standing()
        || readiness_.recovering()
        || rebooting_()
        || phase_.in_stage<phase::Refused>() != nullptr)
        return;
    if (builder_.bridge_refused())
        end_bridge_session_();
}

void SessionOwner::end_bridge_session_() noexcept {
    owner_events_.push(
        Event::SessionFailed{.why = Errc::bridge_state},
        Event::Head{EmitSite{ERR_SITE()}, {}, switch_.pending_tag()}
    );

    phase::Facts f = facts_of(switch_, negotiated_, reboot_, Errc::bridge_state);
    f.front_end = owe_front_end_always_(switch_.pending());
    f.by_bridge = true;
    judge_(phase_.take(phase::Stimulus::BridgeEnded, f), phase::Stimulus::BridgeEnded);
}

void SessionOwner::give_up_start_(Errc why, phase::Stimulus on) noexcept {

    if (why == Errc::timeout) {
        ++fallbacks_.start_timeouts;
        fallback_cell_.publish(fallbacks_);
    }
    refuse_started_switch_(why, on);
}

bool SessionOwner::owe_front_end_(const UiRequest::LoadCore& pending) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    if (front_end_armed_)
        return true;
    if (names_front_end_image(pending.path.view(), pending.xml)) {
        ++fallbacks_.front_end_failed;
        fallback_cell_.publish(fallbacks_);
    }
    return false;
}

bool SessionOwner::owe_front_end_always_(const UiRequest::LoadCore& pending) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    if (!names_front_end_image(pending.path.view(), pending.xml))
        return true;
    ++fallbacks_.front_end_failed;
    fallback_cell_.publish(fallbacks_);
    return false;
}

void SessionOwner::fall_back_to_front_end_() {
    if (!phase_.front_end_owed()
        || before_seats_
        || switch_.switch_standing()
        || readiness_.recovering())
        return;
    if (reboot_.failed()) {
        phase_.drop_front_end();
        return refuse_rebooting_(phase::Stimulus::FrontEnd);
    }

    if (proto::kLinkTxCapacity - inbox_.ring().size() < CoreSwitch::kTeardownOps)
        return;
    const UiRequest::LoadCore req{.path = front_end_image_, .xml = XmlKind::Rbf};
    ask_cause_ = phase::Stimulus::FrontEnd;
    const CoreSwitch::Asked asked = switch_.ask(req, kUncaused, *this);
    ask_cause_ = phase::Stimulus::LoadCore;
    switch (asked) {
        case CoreSwitch::Asked::Yes:
            ++fallbacks_.front_end_asked;
            break;
        case CoreSwitch::Asked::Invalid:
            phase_.drop_front_end();
            ++fallbacks_.front_end_failed;
            break;
        case CoreSwitch::Asked::NoRoom:
            return;
    }
    fallback_cell_.publish(fallbacks_);
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
    phase_edge_(phase::Stimulus::BootLoad);

    front_end_armed_ = !req.path.empty() && !names_front_end_image(req.path.view(), req.xml);
    before_seats_ = true;
    order_hold_reset_();
    order_bind_decoders_(proto::LinkOp::DecoderTable::PreSession, kBootGen);

    const CoreSwitch::StepArg arg = switch_.step_arg(switch_.program_at_boot(*this));
    switch_.latch_step_arg(arg);
    (void)order_refusable_(
        proto::LinkOp::ApplyCore{.loaded = arg.loaded, .gen = as_generation(kBootGen)}
    );
    phase_edge_(phase::Stimulus::BootApply);
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

void SessionOwner::finish_pick_(UiRequest::Kind asked, CorrelationTag tag, ContentPick::Out&& out) {
    offer_(std::move(out.orders));
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
    const auto hold = belt_.savestate_hold();
    if (!hold)
        return;
    if (!savestates_answered_(hold->gen)) {
        if (clock_.now().count() < hold->due_ns)
            return;
        ++tally_.savestate_hold_expiries;
    }
    const PickedLoad load = belt_.take_savestate_hold()->load;
    after_content_(content_.run_attached_load(load, *this));
}

bool SessionOwner::content_ordered_() const noexcept {
    const bool rom_waits = mounts_.save_holds_load();
    return !content_.picked_load() && !belt_.any_live(kContentOwing) && !rom_waits;
}

std::optional<std::uint32_t> SessionOwner::attach_row_savestates(
    bool opensave,
    std::string_view rom
) {
    TASTY_SEAT_BODY(SessionOwner);
    const auto& conf = negotiated_.conf_str();
    if (!opensave || !conf || !conf->savestate().has_value())
        return std::nullopt;
    const std::uint32_t gen = ladder_host_.next_generation();
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
    belt_.hold(LoadBelt::SaveStateHold{.load = load, .gen = gen, .due_ns = due_ns});
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
    return belt_.wait(*this);
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

            if (belt_.addons_after_owed() && content_ordered_())
                belt_.promote_addons_after();
            return;
        case RungSlot::Watch:
            offer_(content_.step_watch());
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

SessionOwner::Wait SessionOwner::held_wait(RungSlot slot) const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    const auto each = [](bool live, bool now) noexcept {
        return !live ? Wait::None : (now ? Wait::Now : Wait::Later);
    };
    const bool room = inbox_.ring().size() < proto::kLinkTxCapacity;
    switch (slot) {
        case RungSlot::BootLadder:
            return each(ladder_.live(), (ladder_.moved() || belt_.walk_answered()) && room);
        case RungSlot::SavestateHold: {
            const auto hold = belt_.savestate_hold();
            return each(hold.has_value(), hold && savestates_answered_(hold->gen));
        }
        case RungSlot::SaveMount:
            return each(mounts_.save_mount_live(), false);
        case RungSlot::AddonsAfter:
            return each(belt_.addons_after_owed(), content_ordered_());
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

void SessionOwner::content_loaded() {
    TASTY_SEAT_BODY(SessionOwner);
    publish_done(content_.content_owed().which, content_.content_owed().tag);
}

void SessionOwner::addons_closed() {
    TASTY_SEAT_BODY(SessionOwner);
    if (auto& picked = content_.picked_load()) {
        const PickedLoad load = *picked;
        picked.reset();
        run_load_(load);
    }
}

void SessionOwner::last_piece_sent(FileTxPieces& pieces) {
    TASTY_SEAT_BODY(SessionOwner);
    content_.watch_last_piece(pieces);
}

void SessionOwner::manifest_failed(Errc why) {
    TASTY_SEAT_BODY(SessionOwner);
    if (rebooting_())
        return refuse_rebooting_(phase::Stimulus::ManifestFailed);
    give_up_start_(why, phase::Stimulus::ManifestFailed);
}

void SessionOwner::manifest_done() {
    TASTY_SEAT_BODY(SessionOwner);
    finish_core_made_();
}

void SessionOwner::offer_(BeltOrders&& orders) {
    if (belt_.offer(std::move(orders)) || !log_)
        return;
    (void)log_->push(LogRec::Refusal{Errc::busy, ERR_SITE()}, log_now_ns_());
}

bool SessionOwner::order_walk(proto::IoIndex slot, std::string_view path, std::uint32_t gen) {
    TASTY_SEAT_BODY(SessionOwner);
    if (belt_.any_live(kWalkBlockers))
        return false;
    content_.content_owed() = Owed{};
    belt_.refuse_walk(gen);
    const auto row = cores::find_core(negotiated_.core_name().view());
    if (!row || (*row)->make_loader == nullptr)
        return true;
    const bool turbo = negotiated_.conf_str() && negotiated_.conf_str()->declares_turbo();
    auto loader = (*row)->make_loader(
        cores::LoaderContext{vfs_, *(*row)->profile, turbo, content_.loader_memo(), aperture_}
    );
    const cores::LoadAsk ask{.index = proto::WideIoIndex{slot.v}, .path = path, .disc = true};
    const auto plan = loader->plan(ask, ladder_host_.status_word());
    auto w = plan ? LoadWalk::start(std::move(loader), *plan, (*row)->profile->windows)
                  : Ex<LoadWalk>{std::unexpected(plan.error())};
    if (!w) {
        ++tally_.files_failed;
        return true;
    }
    content_.forget_watch();
    belt_.offer(std::move(*w), gen);
    return true;
}

void SessionOwner::begin_companion(std::string_view path) {
    TASTY_SEAT_BODY(SessionOwner);
    if (!companion_.present())
        return;
    if (belt_.companion_walk()) {
        ++tally_.companion_skips;
        return;
    }
    auto w = companion_.plan(path);
    if (!w) {
        ++tally_.companion_skips;
        return;
    }
    content_.forget_watch();
    belt_.offer(std::move(*w));
    ++tally_.companion_walks;

    if (!belt_.companion_walk()->loads())
        step_companion_();
    if (belt_.companion_walk() && !belt_.companion_walk()->loads())
        step_companion_();
}

void SessionOwner::after_content_(ContentPick::After&& after) {
    offer_(std::move(after.orders));
    if (after.companion)
        begin_companion(after.companion->view());
}

void SessionOwner::step_companion_() {
    belt_.step_companion(*this, ladder_host_);
}

void SessionOwner::bios_missing_() {

    owner_events_.push(
        Event::InfoRequest{.id = InfoId::CdBiosMissing},
        Event::Head{EmitSite{ERR_SITE()}, {}, kUncaused}
    );
}

cores::ILadderHost& SessionOwner::ladder_host() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return ladder_host_;
}

bool SessionOwner::arm_loader(
    const ContentRequest& req,
    std::string_view save,
    std::uint64_t save_bytes
) {
    TASTY_SEAT_BODY(SessionOwner);
    ContentPick::Armed a = content_.arm_loader(req, save, save_bytes);
    offer_(std::move(a.orders));
    return a.taken;
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
    (void)belt_.take_walk_answer();
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
            owner_events_.push(Event::SdActivity{.slot = end.slot}, head);
        publish_done(end.owed.which, end.owed.tag);
    } else {
        owner_events_.push(Event::InfoRequest{.id = InfoId::ImageMountFailed}, head);
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
    belt_.cancel();
    content_.drop_watch();
    content_.picked_load().reset();
    made_.reset();
    reset_owed_.reset();

    if (boot_restore_ == BootRestore::Laddered)
        boot_restore_ = BootRestore::Asked;
    negotiated_.clear_core_name();
}

void SessionOwner::forget_core_() noexcept {
    belt_.drop_companion();
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

void SessionOwner::order_make_core_(
    bool fabric,
    bool identity_bound,
    const svc::ConfigSnapshot& cfg
) {
    belt_.drop_manifest();
    const std::string_view rel = make_core_rel_();

    const std::string built_from{rel.empty() ? boot_companion_.view() : rel};
    boot_companion_ = PathText{};
    negotiated_.note_build(built_from);
    const auto& conf = negotiated_.conf_str();
    proto::LinkOp::AdoptCore op = builder_.build(
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

    const cores::ManifestDocFacts doc = op.outcome == proto::LinkOp::Built::Core
            && !built_from.empty()
        ? build_doc_facts(vfs_, negotiated_.manifest_text(), built_from)
        : cores::ManifestDocFacts{};

    const IdentityRecord::Stamp stamp = identity_stamp_ = next_identity_stamp(identity_stamp_);
    op.identity = stamp;
    const IdentityRecord::Inputs in{
        .identity_bound = identity_bound,
        .fields = conf ? bind_identity_fields(*conf) : std::array<std::string_view, 4>{},
        .button_override = {doc.buttons_names, doc.buttons_default},
        .gamepad_defaults = cfg.gamepad_defaults != 0,
        .log_file_entry = cfg.log_file_entry != 0,
        .stamp = stamp
    };
    identity_record_cell_.publish(build_identity_record(in, negotiated_.pending_facts()));

    if (!push_gating(inbox_, tally_, ops_.sleeper, infra::make<proto::LinkOp>(op))) {
        builder_.forget(op);
        give_up_start_(Errc::would_block, phase::Stimulus::MakeRefused);
        return;
    }

    if (negotiated_.made_with_manifest() && conf && conf->savestate().has_value()) {
        (void)ask_savestates_(negotiated_.manifest_rel().view(), ladder_host_.next_generation());
    }

    phase_.rebound(os::Deadline::in(clock_, std::chrono::milliseconds{start_bound_ms_}));
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
    owner_events_.push(
        Event::RequestDone{.which = which},
        Event::Head{EmitSite{ERR_SITE()}, {}, tag}
    );
}

void SessionOwner::publish_refusal(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    owner_events_.push(
        Event::RequestRefused{.which = which, .why = code},
        Event::Head{EmitSite{ERR_SITE()}, {}, tag}
    );
}

void SessionOwner::abort_start_(Errc why) noexcept {
    switch_.end(proto::LinkOp::AbortSwitch{.why = why, .gen = as_generation(switch_.gen())});
    order_bind_decoders_(proto::LinkOp::DecoderTable::Census, switch_.gen());
}

void SessionOwner::refuse_started_switch_(Errc why, phase::Stimulus on) noexcept {

    owner_events_.push(
        Event::SessionFailed{.why = why},
        Event::Head{EmitSite{ERR_SITE()}, {}, switch_.pending_tag()}
    );
    abort_start_(why);

    phase::Facts f = facts_of(switch_, negotiated_, reboot_, why);
    f.front_end = owe_front_end_(switch_.pending());
    judge_(phase_.take(on, f), on);
}

void SessionOwner::expect_boot_negotiate() noexcept {
    switch_.latch_pending(UiRequest::LoadCore{}, kUncaused);
    switch_.latch_step_arg(CoreSwitch::StepArg{.loaded = true});
    front_end_armed_ = true;
    before_seats_ = true;
    phase_edge_(phase::Stimulus::BootAdopt);
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
    return ladder_.arm_core(std::nullopt, config, **row, ladder_host_, Owed{});
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
    if (rebooting_())
        return refuse_rebooting_(phase::Stimulus::StartRefused);
    if (const auto* refused = phase_.in_stage<phase::Refused>()) {

        if (refused->by_bridge && refused->gen.v == switch_.gen()) {
            abort_start_(e.err);
            return;
        }
        ++tally_.stale_events;
        return;
    }
    refuse_started_switch_(e.err, phase::Stimulus::StartRefused);
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
    if (rebooting_())
        return refuse_rebooting_(m.made ? phase::Stimulus::CoreMade : phase::Stimulus::CoreRefused);
    if (phase_.in_stage<phase::Refused>() != nullptr) {
        ++tally_.stale_events;
        return;
    }
    phase_.end_bound();
    boot_restore_ = BootRestore::None;
    negotiated_.note_made(m.made);
    if (m.made) {
        belt_.drop_companion();
        companion_.make();
    }
    if (!m.made) {

        ++tally_.core_make_refusals;
        refuse_started_switch_(m.err, phase::Stimulus::CoreRefused);
        return;
    }
    made_ = MadeFacts{.restore_owed = m.restore_owed, .cheat_records = m.cheat_records};
    phase_edge_(phase::Stimulus::CoreMade);

    if (arm_manifest_())
        return;
    finish_core_made_();
}

bool SessionOwner::arm_manifest_() {
    belt_.drop_manifest();
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
    belt_.offer(ManifestLadder{std::move(stager), negotiated_.manifest_rel()});
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
    if (rebooting_())
        return refuse_rebooting_(phase::Stimulus::SessionUp);
    phase_edge_(phase::Stimulus::SessionUp);
    const std::vector<RememberedFile> remembered = read_remembered_files_();

    const bool index0_taken = std::ranges::any_of(remembered, [](const RememberedFile& f) noexcept {
        return f.row.ioctl_index == 0;
    });

    builder_.raise_osd();

    proto::LinkOp::SessionUp up{};
    up.rtc.rtc[7] = 0x40;
    const std::time_t now = std::time(nullptr);
    if (now == static_cast<std::time_t>(-1)) {
        std::fprintf(stderr, "mister: RTC clock read failed; the core starts with a zero date\n");
    } else if (const auto image = proto::rtc_image_at(static_cast<std::uint32_t>(now)); image) {
        up.rtc = *image;
    } else {
        std::fprintf(
            stderr,
            "mister: RTC image failed (code %u site %u); zero date sent\n",
            static_cast<unsigned>(image.error().code),
            static_cast<unsigned>(image.error().site)
        );
    }

    (void)push_gating(inbox_, tally_, ops_.sleeper, infra::make<proto::LinkOp>(up));

    builder_.raise_sector();

    builder_.raise_sys();

    const bool ladder = ladder_.arm_start(negotiated_.core_name(), index0_taken, ladder_host_);
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
            cr.slot = f.dest;
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
                .wire_index = dest,
                .cut_on_hold = true
            }
        )) {
        ++tally_.op_drops;
        return;
    }
    ++tally_.files_loaded;
}

void SessionOwner::on(const proto::LinkEvent::ConfStr& c) {
    log_conf_str_(c);
    if (c.bind_gen.v != switch_.gen()) {
        ++tally_.stale_events;
        return;
    }
    if (rebooting_()) {
        const auto* w = phase_.in_stage<phase::AwaitConfStr>();
        return refuse_rebooting_(
            w != nullptr && w->loaded ? phase::Stimulus::ConfStrAccepted
                                      : phase::Stimulus::ConfStrUnawaited
        );
    }

    if (phase_.in_stage<phase::Refused>() != nullptr) {
        ++tally_.stale_events;
        return;
    }
    const auto* wait = phase_.in_stage<phase::AwaitConfStr>();
    const bool awaited = wait != nullptr && wait->loaded;

    if (c.read == proto::LinkEvent::ConfStrRead::Refused) {
        if (!awaited) {
            ++tally_.link_event_unhandled;
            return;
        }
        refuse_started_switch_(c.err, phase::Stimulus::ConfStrRefused);
        return;
    }
    const auto accepted = negotiated_.accept(c, aperture_);
    if (!accepted) {

        if (awaited) {
            refuse_started_switch_(accepted.error().code, phase::Stimulus::ConfStrRefused);
        } else {
            ++tally_.link_event_unhandled;
        }
        return;
    }
    phase_edge_(awaited ? phase::Stimulus::ConfStrAccepted : phase::Stimulus::ConfStrUnawaited);
    const proto::ConfStr& cs = accepted->get();
    const std::string_view name = cs.name();
    const bool identity_bound = bind_.identity(cs);
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

    order_make_core_(fabric, identity_bound, cfg);
}

void SessionOwner::arm_replay_ini(bool strict) noexcept {
    ini_.arm_replay(strict);
}

bool SessionOwner::launcher_swap_held_() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return before_seats_ || switch_.switch_standing() || readiness_.recovering();
}

bool SessionOwner::launcher_swap_room_() const noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    return proto::kLinkTxCapacity - inbox_.ring().size() >= CoreSwitch::kTeardownOps;
}

void SessionOwner::on(const proto::LinkEvent::IdentityMismatch& w) noexcept {
    ++tally_.identities_fail;
    if (w.bind_gen.v != switch_.gen())
        return;
    if (rebooting_())
        return refuse_rebooting_(
            w.at_accept ? phase::Stimulus::AcceptRefused : phase::Stimulus::IdentitySpent
        );
    const auto* wait = phase_.in_stage<phase::AwaitConfStr>();
    if (w.at_accept && wait != nullptr && wait->loaded) {

        if (w.err == Errc::fpga_not_ready && phase_.cores_made() != 0) {
            phase_.wedge();
            ++tally_.fabric_wedges;

            order_bind_decoders_(proto::LinkOp::DecoderTable::Census, switch_.gen());
            reboot_.settle(true);
            phase_edge_(phase::Stimulus::Reboot);
            return;
        }
        refuse_started_switch_(w.err, phase::Stimulus::AcceptRefused);
        return;
    }
    if (phase_.spend_identity_try(identity_poll_budget_))
        give_up_start_(w.err, phase::Stimulus::IdentitySpent);
}

void SessionOwner::on(const proto::LinkEvent::IdentityMatched& w) noexcept {
    ++tally_.identities_ok;
    if (w.bind_gen.v != switch_.gen())
        return;
    if (rebooting_())
        return refuse_rebooting_(phase::Stimulus::IdentityMatched);
    negotiated_.note_identity(w);

    if (phase_.in_stage<phase::AwaitIdentity>() == nullptr) {
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
    phase_edge_(phase::Stimulus::IdentityMatched);
}

void SessionOwner::switch_minted(const UiRequest::LoadCore& req) noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    negotiated_.forget_identity();
    readiness_.clear_owed();

    front_end_armed_ = !names_front_end_image(req.path.view(), req.xml);

    readiness_.disarm();
    launcher_.withdraw();
    note_memsz_(req);
    phase_edge_(ask_cause_);
    phase_edge_(phase::Stimulus::ParkPushed);
}

void SessionOwner::drop_core_acts() {
    TASTY_SEAT_BODY(SessionOwner);
    drop_ladder_();
    content_.loader_memo() = cores::LoaderMemo{};
}

void SessionOwner::forget_core() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    boot_companion_ = PathText{};
    forget_core_();
}

void SessionOwner::release_and_await_identity_(std::uint32_t gen) noexcept {
    order_(proto::LinkOp::ReleaseReset{});
    order_bind_decoders_(proto::LinkOp::DecoderTable::PreSession, gen);
    negotiated_.note_width(false);
}

void SessionOwner::pause_wedged() noexcept {
    TASTY_SEAT_BODY(SessionOwner);
    reboot_.settle(true);
    phase_edge_(phase::Stimulus::PauseWedged);
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
    if (rebooting_()) {
        refuse_rebooting_(phase::Stimulus::RecoveryAsk);
        return false;
    }

    if (phase::kind(phase_.now) != phase::PhaseKind::Recovering)
        phase_edge_(phase::Stimulus::ReadyLost);
    ask_cause_ = phase::Stimulus::RecoveryAsk;
    switch_.ask_or_refuse(launcher_.alias(switch_.pending()), switch_.pending_tag(), *this);
    ask_cause_ = phase::Stimulus::LoadCore;
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
    const bool owed = switch_.load_held() || (phase_.front_end_owed() && !before_seats_);
    const bool ask_due = owed && !switch_.switch_standing() && !readiness_.recovering();
    if (switch_.last_order_owed() || ask_due)
        return floor_ms < kLastOrderRetryMs ? floor_ms : kLastOrderRetryMs;

    if (rung_wait_() != Wait::None && floor_ms > kPieceRetryMs)
        floor_ms = kPieceRetryMs;
    if (const auto due = phase_.bound(); due && start_bound_ms_ != 0)
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
    phase_edge_(phase::Stimulus::RebootQuiesced);
}

void SessionOwner::phase_edge_(phase::Stimulus on, Errc why) noexcept {
    judge_(phase_.take(on, facts_of(switch_, negotiated_, reboot_, why)), on);
}

void SessionOwner::judge_(const phase::SwitchPhase::Taken& t, phase::Stimulus on) noexcept {
    if (t.origin != phase::Origin::Tolerance)
        return;
    if (log_) {
        (void)log_->push(
            LogRec::PhaseTolerated{
                .gen = t.gen,
                .phase = static_cast<std::uint8_t>(t.at),
                .under = static_cast<std::uint8_t>(t.under),
                .stimulus = static_cast<std::uint8_t>(on)
            },
            log_now_ns_()
        );
    }
    if constexpr (kSeatChecksEnabled) {
        char text[112];
        const auto name = [](std::string_view v) {
            return static_cast<int>(v.size());
        };
        const std::string_view s = phase::kStimulusNames[static_cast<std::size_t>(on)];
        const std::string_view p = phase::kPhaseNames[static_cast<std::size_t>(t.at)];
        const std::string_view u = phase::kPhaseNames[static_cast<std::size_t>(t.under)];
        if (t.at == t.under) {
            (void)std::snprintf(
                text,
                sizeof text,
                "SWITCH PHASE: tolerated %.*s in %.*s",
                name(s),
                s.data(),
                name(p),
                p.data()
            );
        } else {
            (void)std::snprintf(
                text,
                sizeof text,
                "SWITCH PHASE: tolerated %.*s in %.*s over %.*s",
                name(s),
                s.data(),
                name(p),
                p.data(),
                name(u),
                u.data()
            );
        }
        fatal(Error{Errc::negotiation, ERR_SITE(), 0}, text);
    }
}

void SessionOwner::end_rebooting_switch_() noexcept {
    if (switch_.drop_receipt())
        refuse_rebooting_(phase::Stimulus::ParkComplete);
    owner_events_.push(
        Event::SessionFailed{.why = Errc::negotiation},
        Event::Head{EmitSite{ERR_SITE()}, {}, switch_.pending_tag()}
    );
    abort_start_(Errc::negotiation);
}

bool SessionOwner::rebooting_() const noexcept {
    return reboot_.owed().has_value() || reboot_.failed();
}

void SessionOwner::refuse_rebooting_(phase::Stimulus on) noexcept {
    if (phase_.refuse(on))
        return;
    if constexpr (kSeatChecksEnabled)
        fatal(
            Error{Errc::negotiation, ERR_SITE(), 0},
            "SWITCH PHASE: a reboot's refusal found no Refusal row"
        );
}

void SessionOwner::log_conf_str_(const proto::LinkEvent::ConfStr& c) noexcept {
    if (!log_)
        return;
    const auto* wait = phase_.in_stage<phase::AwaitConfStr>();
    const bool awaited = c.bind_gen.v == switch_.gen() && wait != nullptr && wait->loaded;
    (void)log_->push(
        LogRec::ConfStrArrived{
            .bind_gen = c.bind_gen.v,
            .phase = static_cast<std::uint8_t>(phase::kind(phase_.now)),
            .under = static_cast<std::uint8_t>(phase::kind(phase_.stage())),
            .awaited = static_cast<std::uint8_t>(awaited ? 1 : 0)
        },
        log_now_ns_()
    );
}

std::uint64_t SessionOwner::log_now_ns_() const noexcept {
    return static_cast<std::uint64_t>(clock_.now().count());
}

void SessionOwner::phase_after_watch_(bool reboot) noexcept {
    if (reboot)
        return phase_edge_(phase::Stimulus::ProbeExhausted);
    if (phase::kind(phase_.now) != phase::PhaseKind::Recovering || readiness_.recovering())
        return;
    if (switch_.switch_standing())
        return phase_edge_(phase::Stimulus::ProbeOk);

    if (phase_.in_stage<phase::Refused>() != nullptr)
        return phase_edge_(phase::Stimulus::ProbeOkRefused);
    if (phase_.in_stage<phase::AwaitConfStr>() != nullptr || failed_apply_())
        return phase_edge_(phase::Stimulus::ProbeOkAwaiting);
    if (const auto* b = phase_.in_stage<phase::Building>(); b != nullptr && b->bound)
        return phase_edge_(phase::Stimulus::ProbeOkBuilding);
    phase_edge_(phase::Stimulus::ProbeOkUnasked);
}

bool SessionOwner::failed_apply_() const noexcept {
    return !switch_.pending_step_arg().loaded && switch_.tally().programs_failed != 0;
}

}
