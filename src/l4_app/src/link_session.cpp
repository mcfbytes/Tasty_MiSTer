// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_session.h"

#include "app/log_clock.h"
#include "app/disk_relay.h"
#include "app/mailbox_relay.h"
#include "app/save_upload_relay.h"
#include "infra/byte_view.h"
#include "infra/posix_compat.h"

#include "app/config_apply.h"

#include "app/core_init.h"

#include "svc/chd_prefetch.h"

#include "app/input_wire.h"

#include "app/osd_wire.h"
#include "app/rt_relax_scope.h"
#include "app/window_job_service.h"
#include "cores/window_job.h"
#include "app/ini_parse.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "app/link_event_facts.h"
#include "app/link_op_dispatch.h"
#include "app/link_op_facts.h"
#include "app/cheat_apply.h"
#include "app/encoder_roles.h"
#include "proto/image_bracket.h"
#include "proto/osd_surface.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <span>
#include <utility>
#include <vector>

#include "app/bitstream_programmer.h"
#include "app/durable_write.h"
#include "app/durable_write_service.h"
#include "app/session_seats.h"
#include "app/video_wire.h"
#include "cores/cd_core.h"
#include "cores/config_slots.h"
#include "cores/dip_switches.h"
#include "cores/option_rows.h"
#include "cores/save_upload.h"
#include "cores/staging_core.h"
#include "cores/mounted_path.h"
#include "cores/generic_core.h"
#include "reactor/executive.h"
#include "svc/audio_service.h"
#include "svc/config.h"
#include "svc/config_parser.h"
#include "svc/config_snapshot.h"
#include "proto/save_request.h"
#include "proto/spi_fio_queue.h"
#include "proto/spi_frame.h"
#include "svc/vfs.h"

namespace mister::app {

StdoutRouter::~StdoutRouter() {

    if (orig_)
        set_stdout_file(&*orig_);
}

Ex<void> StdoutRouter::install_initial_silence() {

    if (!orig_)
        orig_ = infra::opt_ref_of(stdout);
    if (!null_) {
        std::FILE* f = std::fopen(null_path_, "w");
        if (f == nullptr) {

            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        const int fd = ::fileno(f);
        if (fd >= 0)
            (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
        null_.reset(f);
        set_stdout_file(f);
        sink_ = Sink::Null;
    }

    if (!dbg_) {
        std::FILE* f = std::fopen(debug_path_, "w");
        if (f != nullptr) {
            (void)std::setvbuf(f, nullptr, _IONBF, 0);
            const int fd = ::fileno(f);
            if (fd >= 0)
                (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
            dbg_.reset(f);
        }
    }
    return {};
}

Ex<void> StdoutRouter::route(std::uint8_t debug) {
    if (!orig_)
        orig_ = infra::opt_ref_of(stdout);
    const Sink target = sink_for(debug);

    infra::OptRef<std::FILE> to{};
    switch (target) {
        case Sink::Null:
            to = infra::opt_ref_of(null_.get());
            break;
        case Sink::Original:
            to = orig_;
            break;
        case Sink::DebugFile:
            to = infra::opt_ref_of(dbg_.get());
            break;
    }

    if (!to) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(target)});
    }
    set_stdout_file(&*to);
    sink_ = target;
    return {};
}

namespace {

LinkBinder::Wiring binder_wiring(const SupervisorParts& parts) noexcept {
    return LinkBinder::Wiring{
        .exec = parts.exec,
        .state = parts.core_state,
        .log_lane = parts.log_lane,
        .feeder = infra::OptRef<PcmRingFeeder>{parts.services.feeder},
        .tenant = infra::OptRef<proto::IFioTenant>{parts.services.saves},
    };
}

}

static constexpr EmitSite kAdviseSite{ERR_SITE()};

LinkSession::LinkSession(
    hal::ISpiTransport& link,
    hal::ICoreSignals& signals,
    hal::ISpiSampleSource& samples,
    [[maybe_unused]] hal::PhysRegion aperture,
    EventQueue<SeatTag::RT>& events,
    const SupervisorParts& parts
) noexcept
    : link_(link),
      binder_(link, binder_wiring(parts)),
      fenced_signals_(signals, binder_.fio_queue()),
      machine_(events, parts.log_lane),
      vfs_(parts.vfs),
      discs_(parts.services.discs),
      unbound_discs_(parts.unbound_discs),
      mailbox_(parts.services.mailbox),
      disks_(parts.services.disks),
      saves_(parts.services.saves),
      nvram_(
          link,
          parts.vfs,
          parts.services.writes,
          saves_,
          parts.board_ops.sleeper,
          parts.log_lane
      ),
      window_jobs_(parts.services.window_jobs),
      link_inbox_(parts.link_inbox),
      ui_inbox_(parts.ui_inbox),
      input_inbox_(parts.input_inbox),
      link_rx_(parts.link_rx),
      router_(parts.router),
      out_{router_},
      sampler_(samples, router_, static_cast<const proto::ISessionLive&>(*this), parts.pin_levels),
      config_cell_(parts.config_cell),
      identity_record_cell_(parts.identity_record_cell),
      boot_(machine_, fenced_signals_),
      config_(config_cell_, boot_),
      video_(parts.video),
      input_(parts.input),
      osd_(parts.osd),
      osd_mask_decoder_(link, router_, static_cast<const proto::IOsdFocus&>(*this)),
      ops_(parts.board_ops),
      reboot_(
          RebootAnswer::Wiring{
              .signals = fenced_signals_,
              .nvram = nvram_,
              .window_jobs = infra::OptRef{parts.services.window_jobs},
              .storage_life = parts.services.storage_life
          }
      ),
      switch_park_(parts.main_wake),

      session_(link, fenced_signals_, &binder_.fio_queue()),
      uart_(parts.uart_handoffs),
      log_lane_(parts.log_lane.producer()),
      ladder_cell_(parts.ladder_cell),
      cheats_(parts.cheats),
      save_copy_(parts.save_copy),
      options_(parts.vfs),
      clock_(parts.clock),
      terms_{
          .link = link,
          .clock = parts.clock,
          .bulk = binder_.grant_bulk(),
          .fio_queue = binder_.fio_queue(),
          .unbound_discs = parts.unbound_discs,
          .vfs = parts.vfs,
          .discs = infra::OptRef{parts.services.discs},
          .prefetch = infra::OptRef{parts.services.prefetch},
          .mailbox = infra::OptRef<cores::IMailboxPort>{parts.services.mailbox},
          .disk = infra::OptRef<cores::IDiskPort>{parts.services.disks},
          .save_copy = parts.save_copy,
          .feeder = infra::OptRef{parts.services.feeder},
          .aperture = parts.fpga_mem,
          .doorbells = parts.doorbells,
          .lw_window = parts.lw_window,
          .lw_image = parts.lw_image,
          .doorbell_nodes = parts.doorbell_nodes,
          .doorbell_cpus = parts.doorbell_cpus
      },
      slot_bindings_(
          SlotBindings::Wiring{
              .link = link,
              .binder = binder_,
              .clock = clock_,
              .ops = parts.board_ops,
              .cheats = cheats_,
              .link_rx = link_rx_,
              .machine = machine_,
              .advise_site = kAdviseSite,
              .session_tally = tally_,
              .options = options_,
              .log_lane = parts.log_lane,
              .vfs = parts.vfs,
              .storage_life = parts.services.storage_life,
              .prefetch = infra::OptRef{parts.services.prefetch},
              .storage_channel = parts.storage_channel
          }
      ),
      rows_(
          SessionRows::Wiring{
              .link = link,
              .signals = fenced_signals_,
              .router = router_,
              .live = static_cast<const proto::ISessionLive&>(*this),
              .session = session_,
              .bindings = bindings_,
              .binder = binder_,
              .slots = slot_bindings_.slots(),
              .osd = osd_,
              .input = input_,
              .status_lw = parts.status_snapshots
          }
      ),
      f12_mod_poll_(
          link,
          router_,
          static_cast<const proto::ISessionLive&>(*this),
          rows_.poll_params(),
          proto::user_io::PollShape::F12ModPoll
      ),
      tenancy_(
          CoreTenancy::Wiring{
              .bay = parts.bay,
              .releases = parts.bay_releases,
              .binder = binder_,
              .session = session_,
              .bindings = bindings_,
              .machine = machine_,
              .advise_site = kAdviseSite,
              .clock = clock_,
              .slots = slot_bindings_.slots(),
              .session_tally = tally_,
              .log_lane = parts.log_lane,
              .storage_life = parts.services.storage_life,
              .mailbox = infra::OptRef{parts.services.mailbox},
              .disks = infra::OptRef{parts.services.disks},
              .discs = infra::OptRef{parts.services.discs}
          }
      ),
      streams_(parts.streams),
      encoder_(
          link,
          fenced_signals_,
          session_,
          osd_,
          slot_bindings_.slots(),
          video_,
          parts.input_emitter
      ),
      held_(
          HeldTransfer::Wiring{
              .link = link,
              .binder = binder_,
              .machine = machine_,
              .advise_site = kAdviseSite,
              .bindings = bindings_,
              .cheats = cheats_,
              .osd = osd_,
              .ops = parts.board_ops,
              .slot_bindings = slot_bindings_,
              .tenancy = tenancy_,
              .session_tally = tally_,
              .window_jobs = infra::OptRef{parts.services.window_jobs}
          }
      ) {
    if (parts.stdout_routing)
        enable_stdout_routing();
}

LinkSession::~LinkSession() = default;

std::string_view LinkSession::conf_str_name() const noexcept {
    return bindings_.core_name();
}

bool LinkSession::core_supports_mgl() const noexcept {

    if (session_.type() == proto::CoreType::SharpMz)
        return false;
    return bindings_.record().mgl_playable;
}

void LinkSession::reset_video_geometry() {
    video_.reset_geometry(core_edge_seq_);
}

void LinkSession::arm_video_prelude() {
    video_.arm_prelude();
}

void LinkSession::publish_core_identity() {
    publish_identity();
}

bool LinkSession::declares_turbo() const noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return bindings_.declares_turbo();
}

void LinkSession::publish_identity() {

    identity_.publish(bindings_.record().identity);
}

void LinkSession::push(const Event::SessionAdvisory& a) {
    machine_.advise(a, kAdviseSite);
}

void LinkSession::take_park_ask_() noexcept {
    const bool was = park_.engaged();
    park_.poll(switch_park_);
    if (!was && park_.engaged())
        encoder_.drop_pads();
}

void LinkSession::publish_fabric_() {

    fabric_.refresh(fabric_cell_, clock().now().count());
}

LinkSession::RoundEntry LinkSession::poll(bool tick) {

    const bool live_at_entry = session_live();

    if (held_.wire_held()) {
        if (tick)
            publish_fabric_();
        take_park_ask_();
        if (held_.hold_wire(park_.asked()))
            return RoundEntry{.live = live_at_entry};
    }

    if (tick && live_at_entry) {

        sampler_.service(held_.fio_held());
        const hal::SpiSample& g = sampler_.level();

        core_ready_ = g.ready;

        encoder_.trust_pads(core_ready_);
        if (core_ready_) {
            const std::uint16_t but = g.buttons;

            if (!video_.prelude_running()) {

                (void)video_.update_but_sw(
                    link_,
                    but,
                    osd_.has_focus(),
                    infra::OptRef<proto::IResetFence>{binder_.fio_queue()}
                );

                if (!osd_rearmed_) {

                    if (!osd_.lit() || osd_.set_show(osd_.show()).has_value()) {
                        osd_rearmed_ = true;
                    }
                }
            }
        }
    }

    if (tick)
        publish_fabric_();

    take_park_ask_();
    return RoundEntry{.live = live_at_entry};
}

void LinkSession::tick(RoundEntry entry) {
    if (held_.wire_held()) {
        publish_diag();
        return;
    }
    const bool live_at_entry = entry.live;

    nvram_.drain_write_verdicts();
    {
        const infra::OptRef<cores::Core> current = tenancy_.core();
        cores::IWindowSave* role = current.has_value() ? current->window_save() : nullptr;
        if (const auto act = window_jobs_.service(role))
            held_.publish_ftx_level(*act, true);
    }

    reboot_.answer_reboot(*this);

    if (live_at_entry && session_live() && core_ready_) {
        video_.on_rt_round(
            link_,
            clock().now().count(),
            infra::OptRef<proto::IResetFence>{binder_.fio_queue()}
        );
    }

    if (const cores::ICdDiagnostics* cd = tenancy_.core().has_value()
            ? tenancy_.core()->cd_diagnostics()
            : nullptr;
        cd != nullptr) {
        if (const std::optional<svc::DiscCounters> dc = cd->disc_counters(); dc.has_value()) {
            disc_sync_decompress_ = dc->sync_decompress;
            disc_park_timeouts_ = dc->park_timeouts;
            disc_prefetch_refusals_ = dc->prefetch_refusals;
        }

        if (const auto f = cd->cd_flow(); f.has_value()) {
            cd_flow_ = *f;
        }
    }

    if (park_.asked()) {

        if (binder_.bridge_windows_live())
            binder_.retry_release();
        const bool torn_down = dropped_gen_ == park_.gen() && encoder_.holds() != holds_at_drop_;

        const bool held_back = !torn_down
            || binder_.bridge_windows_live()
            || encoder_.pads_on_lw()
            || tenancy_.drop_owed();
        park_.step(switch_park_, held_back);
    }

    publish_diag();
}

void LinkSession::deliver(
    const proto::LinkOp& op,
    const ILinkTxReader& inbox,
    bool windows_closed
) noexcept {

    const infra::OptRef<cores::Core> live_core = tenancy_.core();
    const EncoderRoles roles{
        .saves = infra::OptRef<ISaveFlush>{*this},
        .cheats = infra::OptRef<ICheatApply>{cheats_},
        .cheat_call = {.core = live_core, .fio_held = held_.fio_held()},
        .acts = infra::OptRef<ICoreOptionActs>{options_}
    };
    const LinkOpCtx ctx{
        .inbox = inbox,
        .windows_closed = windows_closed,
        .live = session_live_(),
        .starting = start_.on(),
        .fence = binder_.fio_queue(),
        .roles = infra::OptRef<const EncoderRoles>{roles},
        .core = live_core
    };
    const auto kind = infra::kind_of(op);
    ILinkEncoder::Outcome r = ILinkEncoder::Outcome::Dropped;
    if (admitted(window_for(kind), ctx)) {
        switch (target_for(kind)) {
            case OpTarget::Wire:
                r = infra::dispatch<LinkOpRoutes<OpTarget::Wire>>(op, encoder_, ctx);
                break;
            case OpTarget::Session:
                r = infra::dispatch<LinkOpRoutes<OpTarget::Session>>(op, *this, ctx);
                break;
            case OpTarget::Transfer:
                r = infra::dispatch<LinkOpRoutes<OpTarget::Transfer>>(op, held_, ctx);
                break;
            case OpTarget::Mount:
                r = infra::dispatch<LinkOpRoutes<OpTarget::Mount>>(op, slot_bindings_, ctx);
                break;
            case OpTarget::Options:
                r = infra::dispatch<LinkOpRoutes<OpTarget::Options>>(
                    op,
                    options_,
                    ctx,
                    static_cast<CoreOptionCells::Host&>(*this)
                );
                break;
            case OpTarget::kCount:
                r = misrouted(op, ctx);
                break;
        }
    } else if (const auto ftx = infra::as<proto::LinkOp::FileTx>(op); ftx) {

        if (ftx->phase == proto::LinkOp::FileTxPhase::WindowOpen) {
            held_.publish_ftx_level(ftx->act, false);
        } else if (ftx->phase != proto::LinkOp::FileTxPhase::WindowClose) {
            held_.publish_stream(ftx->act, false);
        }
    }

    if (const auto sp = infra::as<proto::LinkOp::StagePayload>(op);
        sp && sp->act != 0 && !held_.copy_posted()) {
        held_.publish_ftx_level(sp->act, r != ILinkEncoder::Outcome::Dropped, true);
    }
    if (r == ILinkEncoder::Outcome::Dropped) {
        ++tally_.link_op_drops;
    } else {
        ++tally_.link_ops_encoded;
        if (r == ILinkEncoder::Outcome::EncodedStatusChange) {
            rows_.publish_status_word();
        }
    }
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::BindDecoders& a,
    const LinkOpCtx&
) noexcept {
    switch (a.table) {
        case proto::LinkOp::DecoderTable::Census: {

            const bool seen = a.gen.v == torn_done_
                || a.gen.v == static_cast<std::uint16_t>(park_.gen());
            torn_gen_ = seen ? 0 : a.gen.v;
            std::array<reactor::LinkDecoderDecl, kMaxServices> rows{};

            const bool dead = machine_.state() == SessionState::Failed;
            if (auto r = rows_.rebind_census(
                    dead ? std::span<const reactor::LinkDecoderDecl>{}
                         : rows_.census_rows(rows, tenancy_.core()),
                    tenancy_.core()
                );
                !r) {
                return ILinkEncoder::Outcome::Dropped;
            }
            return ILinkEncoder::Outcome::Encoded;
        }
        case proto::LinkOp::DecoderTable::PreSession:
            if (auto r = rows_.rebind_pre_session(a.gen, tenancy_.core()); !r) {
                return ILinkEncoder::Outcome::Dropped;
            }
            return ILinkEncoder::Outcome::Encoded;
    }
    return ILinkEncoder::Outcome::Dropped;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::DropCore& a, const LinkOpCtx&) noexcept {
    start_.end(a);

    take_park_ask_();
    if (park_.asked()) {
        dropped_gen_ = park_.gen();
        holds_at_drop_ = encoder_.holds();
    }
    ++core_edge_seq_;
    mailbox_.forget();
    disks_.forget();
    saves_.forget();
    RtRelaxScope scope(ops_);
    if (const auto r = detach(); !r) {
        tally_.last_error = r.error();
        machine_.transition(SessionState::Failed);
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::LoadFacts& a,
    const LinkOpCtx& ctx
) noexcept {
    const infra::OptRef<cores::Core> current = tenancy_.core();

    if (!current.has_value() || machine_.state() == SessionState::Failed) {
        ++tally_.load_facts_refusals;
        return ILinkEncoder::Outcome::Dropped;
    }

    RtRelaxScope scope(ops_);
    if (!current->apply_load_facts(ctx.inbox.bytes(a.facts))) {
        ++tally_.load_facts_refusals;
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::MailboxWrite& a,
    const LinkOpCtx&
) noexcept {
    if (!tenancy_.core().has_value() || machine_.state() != SessionState::Running) {
        ++tally_.mailbox_write_drops;
        return ILinkEncoder::Outcome::Dropped;
    }
    const std::array<std::uint16_t, 4> w{a.opcode, a.words[0], a.words[1], a.words[2]};
    proto::SpiFioQueue& q = binder_.fio_queue();
    bool queued = q.append_command(w).has_value();
    if (!queued) {
        (void)q.flush();
        queued = q.append_command(w).has_value();
    }
    (void)q.flush();
    mailbox_.bind(a.poll, a.gen);
    if (!queued) {
        ++tally_.mailbox_write_drops;
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::AdoptCore& a,
    const LinkOpCtx&
) noexcept {
    if (!bindings_.bind_record(identity_record_cell_, a))
        ++tally_.identity_record_refusals;
    const auto made = tenancy_.adopt(a);

    const std::uint8_t at = tenancy_.adopted_slot();
    const infra::OptRef<const BuiltCore> slot = at < kCoreBaySlots
        ? infra::OptRef<const BuiltCore>{tenancy_.bay().at(at)}
        : infra::OptRef<const BuiltCore>{};
    osd_.bind_rows(slot ? slot->osd_pages : OsdPageRoute::Link);
    encoder_.bind_pads(
        slot && slot->pads ? infra::OptRef<const LwPads>{*slot->pads}
                           : infra::OptRef<const LwPads>{}
    );

    rows_.bind_status_route(
        slot ? slot->status_route : StatusRoute::Link,
        slot ? slot->gen : std::uint16_t{0}
    );

    if (made && slot)
        slot_bindings_.slots().bind_lw_slots(slot->lw_slots);

    rows_.bind_block_poll(made && slot ? slot->block_poll : BlockPollRoute::Link);
    const infra::OptRef<cores::Core> current = tenancy_.core();

    if (!out_.push(
            proto::LinkEvent::CoreMade{
                .made = made.has_value(),
                .restore_owed = made.has_value()
                    && current.has_value()
                    && current->config_slots() != nullptr,
                .cheat_records = made.has_value()
                    && current.has_value()
                    && current->cheat_records() != nullptr,
                .bind_gen = bindings_.generation().v,
                .err = made.has_value() ? Errc{} : made.error().code
            }
        ))
        ++tally_.core_made_drops;
    if (!made) {

        encoder_.drop_pads();
        (void)binder_.unbind_session();
        tally_.last_error = made.error();
        machine_.transition(SessionState::Failed);
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::SessionUp& a,
    const LinkOpCtx&
) noexcept {
    RtRelaxScope scope(ops_);
    if (const auto r = perform_session_up_(a); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::BindFacts& a,
    const LinkOpCtx& ctx
) noexcept {
    bindings_.bind_facts(a, ctx.inbox);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::BindIdentity& a,
    const LinkOpCtx& ctx
) noexcept {
    bindings_.bind_identity(a, ctx.inbox);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::BindDoorbells& a,
    const LinkOpCtx& ctx
) noexcept {
    bindings_.bind_doorbells(a, ctx.inbox);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::BindConfig& a,
    const LinkOpCtx& ctx
) noexcept {

    const ConfigPair::Out bound = config_.bind();
    if (bound.advise)
        push(Event::SessionAdvisory{.why = *bound.advise});
    if (bound.dropped)
        ++tally_.link_op_drops;
    video_.set_key_map(a.conf);
    if (a.waitmount_exhausted) {
        push(Event::SessionAdvisory{.why = Errc::timeout});
    }
    if (a.cfg != proto::TxSlabId{} && session_.type() == proto::CoreType::EightBit) {
        const auto bytes = ctx.inbox.bytes(a.cfg);
        if (bytes.size() == 16) {
            session_.stage(
                proto::StatusEdit::whole(proto::StatusWord::from_bytes(bytes.first<16>()))
            );
        }
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::BindUart& a,
    const LinkOpCtx& ctx
) noexcept {
    bindings_.bind_uart(a, ctx.inbox);

    if (a.probe_usb_ser)
        uart_.latch_usb_ser(a.usb_ser);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::RebootNow& a,
    const LinkOpCtx&
) noexcept {
    switch (reboot_.on_reboot_now(a)) {
        case RebootAnswer::After::EndStartAndFail:
            start_.end(a);
            fail_machine_();
            break;
        case RebootAnswer::After::CountStale:
            ++tally_.stale_owner_steps;
            break;
        case RebootAnswer::After::ReleaseBus:
            release_bus();
            break;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::ApplyCore& a,
    const LinkOpCtx&
) noexcept {
    apply_core_(a);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::AbortSwitch& a,
    const LinkOpCtx&
) noexcept {

    if (park_.engaged() && a.gen.v == static_cast<std::uint16_t>(park_.gen())) {
        park_.withdraw();
        torn_gen_ = 0;
        torn_done_ = a.gen.v;
        push(Event::SessionAdvisory{.why = a.why});
        return ILinkEncoder::Outcome::Encoded;
    }
    if (!start_.on() || a.gen != start_.gen()) {

        if (torn_gen_ != 0
            && a.gen.v == torn_gen_
            && a.gen.v != static_cast<std::uint16_t>(park_.gen())) {
            torn_gen_ = 0;
            torn_done_ = a.gen.v;
            push(Event::SessionAdvisory{.why = a.why});
            return ILinkEncoder::Outcome::Encoded;
        }
        ++tally_.stale_owner_steps;
        return ILinkEncoder::Outcome::Encoded;
    }
    start_.end(a);

    session_.refuse();
    if (machine_.state() == SessionState::Boot)
        machine_.transition(SessionState::Running);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::CoreReset& a,
    const LinkOpCtx& ctx
) noexcept {
    const infra::OptRef<cores::Core> current = tenancy_.core();
    if (!current.has_value())
        return ILinkEncoder::Outcome::Dropped;
    const auto pulse = current->reset(a.edge);
    if (!pulse)
        return ILinkEncoder::Outcome::Dropped;
    if (cores::IWindowSave* role = current->window_save(); role != nullptr) {
        (void)window_jobs_.arm_reset_edge(*role, a.edge);
    }
    if (!*pulse)
        return ILinkEncoder::Outcome::Encoded;
    const auto out = encoder_.on(proto::LinkOp::PulseOption{.bit = **pulse}, ctx);
    if (out == ILinkEncoder::Outcome::EncodedStatusChange)
        ++tally_.resets_pulsed;
    return out;
}

ILinkEncoder::Outcome LinkSession::on(
    const proto::LinkOp::SetVolume& a,
    const LinkOpCtx&
) noexcept {

    video_.audio().set_volume(a.cmd, a.arg);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::misrouted(const proto::LinkOp&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    ++tally_.link_op_misrouted;
    return ILinkEncoder::Outcome::Dropped;
}

static_assert(LinkOpSessionSink<LinkSession>);

template <class A>
void LinkSession::log_edge(const A& alt) noexcept {
    log_lane_.push(alt, log_now_ns());
}

Ex<std::optional<UiRequest::LoadCore>> LinkSession::boot(std::span<const char* const> argv) {
    TASTY_SEAT_BODY(LinkSession);

    if (!boot_.in_boot_state()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (vfs_.has_value())
        publish_fabric_();

    const Ex<LoadRequest> req = boot_.argv_request(argv);
    if (!req)
        return std::unexpected(req.error());
    arm_request(*req);

    encoder_.drop_pads();

    if (!req->path.empty()) {
        UiRequest::LoadCore sw = boot_.program(*req);
        start_.arm_at_boot();
        return sw;
    }

    publish_fabric_();
    adopt_programmed_core_();

    if (auto r = rows_.rebind_pre_session(proto::BindGeneration{kBootGen}, tenancy_.core()); !r) {
        machine_.transition(SessionState::Running);
        return std::unexpected(r.error());
    }
    start_.arm_at_boot();
    return std::nullopt;
}

Ex<void> LinkSession::load_core(const LoadRequest& req) {
    if (req.policy == ReloadPolicy::ReExec)
        return boot_.re_exec(req);

    return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
}

Ex<void> LinkSession::reset_core() {

    if (session_.type() != proto::CoreType::EightBit) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(session_.type())}
        );
    }
    return session_.pulse_reset();
}

void LinkSession::apply_core_(const proto::LinkOp::ApplyCore& a) {
    const proto::BindGeneration gen = a.gen;
    torn_gen_ = 0;

    const bool at_boot = gen.v == kBootGen;
    if (!at_boot && (!park_.parked() || gen.v != static_cast<std::uint16_t>(park_.gen()))) {
        ++tally_.stale_owner_steps;
        return;
    }
    ++core_edge_seq_;

    rows_.disarm_polls();
    f12_mod_poll_.arm(false, bindings_.generation());

    if (!at_boot)
        park_.release();

    machine_.adopt_tag(a.tag);

    start_.arm(a);

    if (!a.loaded) {
        refuse_start_(Error{Errc::core_load, ERR_SITE(), 0});
        machine_.transition(SessionState::Running);
        return;
    }

    publish_fabric_();
    adopt_programmed_core_();

    bindings_.forget_core();
}

void LinkSession::refuse_start_(const Error& e) noexcept {
    tally_.last_error = e;
    if (!out_.push(proto::LinkEvent::StartRefused{.bind_gen = start_.gen(), .err = e.code})) {
        ++tally_.start_refused_drops;
    }
}

void LinkSession::adopt_programmed_core_() {

    fenced_signals_.clear_gpo();
    fenced_signals_.set_core_reset(false);
    session_ = proto::CoreSession(
        link_,
        fenced_signals_,
        proto::MemSizeCookie{fabric_.cookie()},
        &binder_.fio_queue()
    );
}

Ex<void> LinkSession::perform_session_up_(const proto::LinkOp::SessionUp& op) {
    if (const auto r = raise_session_(op.rtc); !r) {

        encoder_.drop_pads();
        (void)binder_.unbind_session();
        refuse_start_(r.error());
        machine_.transition(SessionState::Failed);
        return r;
    }

    ever_running_ = true;
    start_.end(op);
    osd_.on_fabric_reprogrammed();
    machine_.transition(SessionState::Running);
    const bool front_end = bindings_.record().identity.front_end;
    cores::CoreInitContext init_ctx{link_, session_, *this, tenancy_.core()};
    const infra::OptRef<cores::Core> current = tenancy_.core();
    (void)run_core_init(
        init_ctx,
        current.has_value() ? current->profile().init_additions
                            : std::span<const cores::CoreInitStep>{}
    );
    rows_.publish_status_word();
    options_.publish_dip_table(tenancy_.core());
    options_.publish_config_slots(tenancy_.core());
    options_.publish_option_table(tenancy_.core());
    osd_mask_decoder_.forget(front_end);
    rows_.arm_session_polls(front_end);
    osd_rearmed_ = false;
    machine_.close(
        Event::CoreLoaded{
            .type = session_.type(),
            .mgl_capable = core_supports_mgl(),
            .front_end = front_end
        },
        EmitSite{ERR_SITE()}
    );
    log_edge(LogRec::CoreLoad{sat_u32((log_now_ns() - load_start_ns_) / 1'000'000u)});
    return {};
}

Ex<ExitReboot> LinkSession::shutdown() {

    struct PublishOnExit {
        LinkSession* self;

        ~PublishOnExit() { self->publish_diag(); }
    } publish_on_exit{this};

    nvram_.settle_save_lane(fio_free_());
    const auto d = detach();

    if (tenancy_.drop_owed())
        tenancy_.settle_retire(CoreTenancy::RetireEnd::Exit);

    publish_fabric_();

    release_bus();

    if (fpga_unconfigured() && ever_running_) {
        boot_.hold_for_reboot();
        fail_machine_();
        return ExitReboot::Cold;
    }
    if (!d)
        return std::unexpected(d.error());
    return ExitReboot::None;
}

void LinkSession::release_bus() noexcept {

    link_.deselect();
    fenced_signals_.set_core_reset(false);
}

void LinkSession::arm_request(const LoadRequest& req) {

    machine_.adopt_tag(kUncaused);

    load_start_ns_ = log_now_ns();
    pending_.arm(req);
}

static_assert(requires(const proto::SpiBlockDecoder& d) { d.osd_claims(); });
static_assert(
    cores::CoreProfile{}.block_drain_budget == proto::SpiBlockDecoder::kDrainBudget,
    "the generic profile's drain bound is stock's four-iteration loop"
);

Ex<void> LinkSession::detach() {

    const std::uint64_t t0 = log_now_ns();

    held_.cut_all();

    binder_.fio_queue().abandon();

    slot_bindings_.drop_bindings();

    encoder_.drop_pads();

    osd_.bind_rows(OsdPageRoute::Link);
    rows_.bind_status_route(StatusRoute::Link, 0);
    rows_.bind_block_poll(BlockPollRoute::Link);

    if (auto r = binder_.unbind_session(); !r)
        return r;

    slot_bindings_.note_drop_park();

    tenancy_.drop(t0, slot_bindings_.channel_borrows());
    slot_bindings_.forget_channel_borrows();
    (void)slot_bindings_.slots().install_completions();
    tenancy_.settle_retire(CoreTenancy::RetireEnd::Poll);

    session_ = proto::CoreSession(link_, fenced_signals_, &binder_.fio_queue());
    return {};
}

void LinkSession::seal_boot_pump() noexcept {
    streams_.seal_boot_pump();
    disks_.seal_boot_pump();
}

void LinkSession::settle_boot_slot_mount() noexcept {
    slot_bindings_.settle_slot_mount(tenancy_.core());
}

void LinkSession::reap_storage() noexcept {
    (void)slot_bindings_.slots().install_completions();
    tenancy_.settle_retire(CoreTenancy::RetireEnd::Round);
    slot_bindings_.settle(tenancy_.core());
}

void LinkSession::service_saves(std::uint32_t budget_words) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    const std::uint32_t drops = saves_.liveness_drops();
    saves_.service_rt(link_, budget_words, fio_free_());
    if (saves_.liveness_drops() != drops)
        log_edge(LogRec::Refusal{Errc::timeout, ERR_SITE()});
}

void LinkSession::gather_diag(DiagCounters& out) const noexcept {
    out.link = static_cast<std::uint8_t>(
        (session_live() ? 1u : 0u) | (park_.parked() ? 2u : 0u) | (start_.on() ? 4u : 0u)
    );
    out.core_shutdowns = tenancy_.tally().core_shutdowns;
    const bool status_lw = rows_.status_route() == StatusRoute::Lw;
    out.stage_status_change = status_lw ? rows_.axi_status_decoder().last()
                                        : rows_.status_decoder().last();
    out.status_reply_refusals = rows_.status_decoder().flush_refusals()
        + rows_.axi_status_decoder().flush_refusals();
    out.status_lw = status_lw ? 1u : 0u;
    out.status_lw_adopts = rows_.axi_status_decoder().adopted();
    out.status_lw_stale = rows_.axi_status_decoder().stale();

    out.prefetch_park_timeouts = slot_bindings_.tally().prefetch_park_timeouts;
    out.stale_owner_steps = tally_.stale_owner_steps;
    out.start_answer_drops = start_answer_drops();
    out.core_abandons = tenancy_.tally().core_abandons;
    out.info_poll_pushes = rows_.info_poll().pushes();
    out.fed_refusals = binder_.fed_refusals();
    out.pads_on_lw = encoder_.pads_on_lw() ? 1u : 0u;
    out.osd_page_rows_link = osd_.page_rows_sent();
    out.osd_status_rows_link = osd_.status_rows_sent();

    out.disc_sync_decompress = disc_sync_decompress_;
    out.disc_park_timeouts = disc_park_timeouts_;
    out.disc_prefetch_refusals = disc_prefetch_refusals_;
    out.cd_flow_waits = cd_flow_.waits;
    out.cd_cdda_sectors = cd_flow_.cdda_sectors;
    out.cd_data_sectors = cd_flow_.data_sectors;
    out.cd_idle_ticks = cd_flow_.idle_ticks;
    out.cd_gated_ticks = cd_flow_.gated_ticks;
    out.cd_drive_state = cd_flow_.drive_state;
    out.cd_drive_track = cd_flow_.drive_track;
    out.cd_drive_lba = cd_flow_.drive_lba;
    out.cd_drive_audio_lba = cd_flow_.drive_audio_lba;
    out.cd_drive_is_data = cd_flow_.drive_is_data;
    out.cd_substitutes = cd_flow_.substitutes;
    out.cd_subcode_substitutes = cd_flow_.subcode_substitutes;
    out.cd_not_resident = cd_flow_.not_resident;
    out.cd_busy_ticks = cd_flow_.busy_ticks;
    out.cd_egress_abandons = cd_flow_.egress_abandons;
    out.last_error_code = static_cast<std::uint16_t>(tally_.last_error.code);
    out.last_error_site = tally_.last_error.site;

    out.last_error_detail = tally_.last_error.detail;

    const LadderCell lad = ladder_cell_.sample().value;
    const cores::BootLadder::Report& rep = lad.report;
    out.ladder_live = lad.live ? 1u : 0u;
    out.ladder_pc = lad.pc;
    out.ladder_rungs = rep.rungs;
    out.ladder_bytes = rep.bytes_downloaded;
    out.ladder_assets = rep.assets_loaded;
    out.ladder_bios_found = rep.bios_found;
    out.ladder_disc_mounted = rep.disc_mounted;
    out.ladder_save_mounted = rep.save_mounted;
    out.file_tx_count = held_.tally().downloads_closed;
    out.open_saves_mounted = held_.tally().open_saves_mounted;
    out.open_save_refusals = held_.tally().open_save_refusals;
    out.file_tx_bytes = held_.last_tx_bytes();
    out.file_tx_index = held_.last_tx_index();

    const proto::SpiBlockDecoder::Counters& bc = rows_.block_decoder().counters();
    out.blk_rounds = bc.passes;
    out.blk_served = bc.served;
    out.blk_errors = bc.errors;
    out.blk_err_code = bc.err_code;
    const proto::BlockSlots::Diagnostics& bd = slot_bindings_.slots().diagnostics();
    const proto::DecodeCounters& dc = slot_bindings_.slots().block_poll().diagnostics();
    out.blk_blocks = bd.blocks_served;
    out.blk_oversize = dc.oversize_requests;
    out.blk_unencodable = bd.unencodable_announce;
    out.blk_blank_filled = bd.blank_filled;
    out.blk_write_failures = bd.write_failures;
    out.blk_stock = dc.stock_requests;
    out.blk_config = dc.config_requests;
    out.blk_discarded = bd.discarded_writes;
    out.blk_deferred = bc.deferred;
    out.blk_expired = bd.answer_expiries;
    out.blk_pf_expired = bd.prefetch_expiries;
    out.blk_staging_expired = bd.staging_expiries;
    out.blk_stale = bd.stale_completions;
}

void LinkSession::publish_diag() noexcept {
    DiagCounters d{};
    gather_diag(d);
    diag_.publish(d);
}

void LinkSession::diag_counters(DiagCounters& out) const noexcept {
    out = diag_.sample().value;
}

void LinkSession::publish_doorbell_stats() noexcept {
    const DoorbellStats d = binder_.doorbell_stats();

    if (d.declared == doorbell_last_.declared
        && d.bound == doorbell_last_.bound
        && d.refusals == doorbell_last_.refusals
        && d.fallbacks == doorbell_last_.fallbacks
        && d.retirements == doorbell_last_.retirements
        && d.release_failures == doorbell_last_.release_failures) {
        return;
    }
    doorbell_last_ = d;
    doorbell_cell_.publish(d);
}

void LinkSession::poll_osd_mask() {

    if (reboot_.rebooting())
        return;

    if (start_.on() || park_.engaged())
        return;
    if (const std::optional<bool> edge = rows_.latch_osd_edge())
        on_osd_visibility_edge(*edge);
    osd_mask_decoder_.service();
}

bool LinkSession::osd_focused() const noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return osd_.has_focus();
}

void LinkSession::on_osd_visibility_edge(bool visible) {
    if (auto r = flush_save_upload(); !r) {
        nvram_.count_error();
        log_edge(LogRec::Refusal{r.error().code, r.error().site});
    }
    const infra::OptRef<cores::Core> current = tenancy_.core();
    if (visible && current.has_value()) {
        if (cores::IWindowSave* role = current->window_save(); role != nullptr) {
            (void)window_jobs_.arm_osd_open(*role);
        }
    }
    if (visible || !current.has_value())
        return;

    ++tally_.osd_close_hooks;
    if (auto r = current->osd_closed(); !r) {
        log_edge(LogRec::Refusal{r.error().code, r.error().site});
    }
}

Ex<void> LinkSession::raise_session_(const proto::RtcImage& rtc) {
    const std::string_view name = conf_str_name();

    const std::string_view eff = effective_name(name, bindings_.facts());

    if (session_.type() == proto::CoreType::EightBit) {

        if (auto r = session_.load_saved_config(); !r)
            return r;

        if (auto r = session_.send_rtc_image(rtc); !r)
            return r;
    }

    slot_bindings_.bind_core_roles(tenancy_.core());

    if (tenancy_.core().has_value()) {
        rows_.bind_block_drain(tenancy_.core());
        std::array<reactor::LinkDecoderDecl, kMaxServices> rows{};
        const SessionBindings::Doorbells& db = bindings_.doorbells();
        const BuiltCore& built = tenancy_.bay().at(tenancy_.adopted_slot());
        if (auto r = binder_.bind_session(
                rows_.census_rows(rows, tenancy_.core()),
                tenancy_.core(),
                db.table(),
                db.declared,
                built.lines
            );
            !r) {
            return r;
        }
    } else if (auto r = rows_.rebind_census({}, tenancy_.core()); !r) {
        return r;
    }

    if (session_.type() == proto::CoreType::EightBit) {
        if (auto r = session_.release_reset(); !r)
            return r;
    }

    {
        const SessionBindings::Uart& bound = bindings_.uart();
        const bool uart_capable = bound.capable;
        const std::string_view uart_token = bound.uart_token.view();
        const std::string_view midi_token = bound.midi_token.view();

        uart_.bind_identity(eff, name);
        UartPersistFiles files{};
        files.ready = true;
        if (bound.mode.has_value()) {
            files.have_mode = true;
            files.mode = *bound.mode;
        }
        if (bound.speeds.has_value()) {
            files.have_speeds = true;
            files.speeds[0] = (*bound.speeds)[0];
            files.speeds[1] = (*bound.speeds)[1];
            files.speeds[2] = (*bound.speeds)[2];
        }
        bindings_.spend_uart_files();

        proto::SpiFrame frame{link_};
        (void)uart_.load_for_core(
            frame,
            uart_capable,
            uart_token,
            midi_token,
            &files,
            bindings_.record().uart_default_baud
        );
    }

    f12_mod_poll_.arm(true, bindings_.generation());
    f12_mod_poll_.start_beat();
    return {};
}

bool LinkSession::session_live() const noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return session_live_();
}

bool LinkSession::save_upload_counted() noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return nvram_.save_upload_counted(save_ctx_());
}

bool LinkSession::options_push_save_bytes(
    proto::SaveKind kind,
    proto::SaveStatus st,
    proto::RxSlabId path,
    proto::RxSlabId data,
    CorrelationTag tag
) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return out_.push(
        proto::LinkEvent::SaveBytes{
            .which = kind,
            .status = st,
            .rel_path = path,
            .payload = data,
            .tag = tag
        }
    );
}

Ex<proto::RxSlabId> LinkSession::options_intern_save_bytes(
    std::span<const std::uint8_t> bytes
) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return out_.intern<proto::LinkEvent::SaveBytes>(bytes);
}

bool LinkSession::reboot_push_quiesced(std::uint16_t seq) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return out_.push(proto::LinkEvent::RebootQuiesced{.seq = seq});
}

NvramUpload::SaveCtx LinkSession::save_ctx_() noexcept {
    const infra::OptRef<cores::Core> core = tenancy_.core();
    return NvramUpload::SaveCtx{
        .core = core,
        .fenced = core.has_value() && held_.save_pull_fenced(tenancy_.core())
    };
}

bool LinkSession::fio_free_() noexcept {
    proto::SpiFioQueue& q = binder_.fio_queue();
    return !held_.fio_held() && q.idle() && !q.held();
}

void LinkSession::fail_machine_() noexcept {
    if (machine_.state() != SessionState::Failed)
        machine_.transition(SessionState::Failed);
}

}
