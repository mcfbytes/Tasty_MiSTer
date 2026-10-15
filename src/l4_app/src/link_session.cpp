// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_session.h"

#include "app/mailbox_relay.h"
#include "infra/byte_view.h"
#include "infra/posix_compat.h"

#include "app/config_apply.h"

#include "app/core_init.h"

#include "svc/chd_prefetch.h"

#include "app/input_wire.h"

#include "app/osd_wire.h"
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
#include "cores/registry.h"
#include "reactor/executive.h"
#include "svc/audio_service.h"
#include "svc/config.h"
#include "svc/config_parser.h"
#include "svc/config_snapshot.h"
#include "proto/save_request.h"
#include "proto/spi_fio_queue.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

const ILinkTxReader* inbox_ptr(infra::OptRef<const ILinkTxReader> r) noexcept {
    return r ? &*r : nullptr;
}

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

XmlKind xml_kind_of(std::string_view s) {
    if (s.size() <= 4) return XmlKind::Rbf;
    const std::string_view tail = s.substr(s.size() - 4);
    if (ieq(tail, ".mra")) return XmlKind::Mra;
    if (ieq(tail, ".mgl")) return XmlKind::Mgl;
    return XmlKind::Rbf;
}

std::uint64_t log_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

std::uint32_t sat_u32(std::uint64_t v) noexcept {
    return v > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(v);
}

class RtRelaxScope {
public:
    explicit RtRelaxScope(const BoardOps& ops) : rt_(ops.rt) {
        if (rt_) rt_->relax();
    }
    ~RtRelaxScope() {
        if (rt_) rt_->restore();
    }
    RtRelaxScope(const RtRelaxScope&) = delete;
    RtRelaxScope& operator=(const RtRelaxScope&) = delete;

private:
    infra::OptRef<IRtPriorityScope> rt_;
};

class SpanFile final : public svc::IFile {
public:
    explicit SpanFile(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (off >= bytes_.size()) return std::size_t{0};
        const std::size_t n = std::min(dst.size(), bytes_.size() - static_cast<std::size_t>(off));
        std::memcpy(dst.data(), bytes_.data() + off, n);
        return n;
    }
    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    [[nodiscard]] Ex<svc::FileSize> size() const override { return svc::FileSize{bytes_.size()}; }
    [[nodiscard]] Ex<void> flush() override { return {}; }
    svc::FileKind kind() const noexcept override { return svc::FileKind::Memory; }

private:
    std::span<const std::uint8_t> bytes_;
};

constexpr std::size_t kConfigRelCap = 256;

constexpr std::int64_t kQuiesceBudgetNs = 250'000'000;

constexpr std::int64_t kCoreRetireBoundNs = 2 * proto::BlockSlots::kStagingDeadlineNs;

FixedStr<kConfigRelCap, StrFit::Reject> config_rel(std::string_view dir, std::string_view name) {
    FixedStr<kConfigRelCap, StrFit::Reject> rel;
    char buf[kConfigRelCap];
    const std::size_t n = dir.size() + name.size();
    if (name.empty() || n > decltype(rel)::kCapacity) return rel;
    std::memcpy(buf, dir.data(), dir.size());
    std::memcpy(buf + dir.size(), name.data(), name.size());
    if (!rel.assign(std::string_view{buf, n})) rel.clear();
    return rel;
}

constexpr std::size_t kSlotPackBytes = 8192;

std::size_t pack_slot_names(cores::IConfigSlots& cs, std::span<std::byte> dst) {
    const auto n =
        static_cast<std::uint8_t>(std::min<std::size_t>(cs.slot_count(), cores::kMaxConfigSlots));
    if (dst.empty()) return 0;
    dst[0] = std::byte{n};
    std::size_t off = 1;
    for (std::uint8_t i = 0; i < n; ++i) {
        const auto name = cs.file_name(cores::ConfigSlot{i});
        if (off + 1 + name.size() > dst.size()) return 0;
        dst[off++] = std::byte{static_cast<std::uint8_t>(name.size())};
        if (!name.empty()) {
            std::memcpy(dst.data() + off, name.view().data(), name.size());
            off += name.size();
        }
    }
    return off;
}

}  // namespace

StdoutRouter::~StdoutRouter() {

    if (orig_) set_stdout_file(&*orig_);
}

Ex<void> StdoutRouter::install_initial_silence() {

    if (!orig_) orig_ = infra::opt_ref_of(stdout);
    if (!null_) {
        std::FILE* f = std::fopen(null_path_, "w");
        if (f == nullptr) {

            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        const int fd = ::fileno(f);
        if (fd >= 0) (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
        null_.reset(f);
        set_stdout_file(f);
        sink_ = Sink::Null;
    }

    if (!dbg_) {
        std::FILE* f = std::fopen(debug_path_, "w");
        if (f != nullptr) {
            (void)std::setvbuf(f, nullptr, _IONBF, 0);
            const int fd = ::fileno(f);
            if (fd >= 0) (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
            dbg_.reset(f);
        }
    }
    return {};
}

Ex<void> StdoutRouter::route(std::uint8_t debug) {
    if (!orig_) orig_ = infra::opt_ref_of(stdout);
    const Sink target = sink_for(debug);

    std::FILE* to = nullptr;
    switch (target) {
        case Sink::Null:
            to = null_.get();
            break;
        case Sink::Original:
            to = orig_ ? &*orig_ : nullptr;
            break;
        case Sink::DebugFile:
            to = dbg_.get();
            break;
    }

    if (to == nullptr) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(target)});
    }
    set_stdout_file(to);
    sink_ = target;
    return {};
}

namespace {

LinkBinder::Wiring binder_wiring(const SupervisorParts& parts) noexcept {
    return LinkBinder::Wiring{
        .exec = parts.exec,
        .state = parts.core_state,
        .log_lane = parts.log_lane,
    };
}

}  // namespace

LinkSession::LinkSession(hal::ISpiTransport& link, hal::ICoreSignals& signals,
                         hal::ISpiSampleSource& samples, [[maybe_unused]] hal::PhysRegion aperture,
                         EventQueue<SeatTag::RT>& events, const SupervisorParts& parts) noexcept
    : link_(link), binder_(link, binder_wiring(parts)),
      fenced_signals_(signals, binder_.fio_queue()), machine_(events, parts.log_lane),
      vfs_(parts.vfs), unbound_discs_(parts.unbound_discs), link_inbox_(parts.link_inbox),
      ui_inbox_(parts.ui_inbox), input_inbox_(parts.input_inbox), link_rx_(parts.link_rx),
      router_(parts.router), out_{router_},
      sampler_(samples, router_, static_cast<const proto::ISessionLive&>(*this), parts.pin_levels),
      config_cell_(parts.config_cell), video_(parts.video), input_(parts.input), osd_(parts.osd),
      ops_(parts.board_ops), quiesce_(parts.main_wake), session_(link, fenced_signals_),
      uart_(parts.uart_handoffs),
      identify_decoder_(fenced_signals_, session_, router_, bindings_.generation()),
      status_decoder_(link, session_, status_cell_, static_cast<const proto::ISessionLive&>(*this)),
      conf_str_decoder_(link, session_, router_, bindings_.generation()), log_lane_(parts.log_lane),
      ladder_cell_(parts.ladder_cell), cheats_(parts.cheats), save_copy_(parts.save_copy),
      clock_(parts.clock), terms_{.link = link,
                                  .clock = parts.clock,
                                  .bulk = binder_.grant_bulk(),
                                  .fio_queue = binder_.fio_queue(),
                                  .unbound_discs = parts.unbound_discs,
                                  .vfs = parts.vfs,
                                  .save_copy = parts.save_copy,
                                  .aperture = parts.fpga_mem,
                                  .doorbells = parts.doorbells,
                                  .lw_window = parts.lw_window,
                                  .doorbell_nodes = parts.doorbell_nodes},
      save_src_(vfs_ ? std::make_unique<SaveImageSource>(*vfs_) : nullptr),
      slots_(proto::BlockSlots::Wiring{
          .source = save_src_ ? infra::OptRef<proto::IImageSource>{*save_src_}
                              : infra::OptRef<proto::IImageSource>{},
          .clock = infra::OptRef<const os::IClock>{clock_},
          .channel = parts.storage_channel}),
      block_decoder_(link, router_, slots_, static_cast<const proto::ISessionLive&>(*this)),
      osd_mask_decoder_(link, router_, static_cast<const proto::IOsdFocus&>(*this)),
      info_poll_(link, router_, static_cast<const proto::ISessionLive&>(*this),
                 static_cast<proto::user_io::IPollParams&>(*this),
                 proto::user_io::PollShape::InfoPoll),
      rumble_poll_(link, router_, static_cast<const proto::ISessionLive&>(*this),
                   static_cast<proto::user_io::IPollParams&>(*this),
                   proto::user_io::PollShape::RumblePoll),
      f12_mod_poll_(link, router_, static_cast<const proto::ISessionLive&>(*this),
                    static_cast<proto::user_io::IPollParams&>(*this),
                    proto::user_io::PollShape::F12ModPoll),
      bay_(parts.bay), releases_(parts.bay_releases), streams_(parts.streams),
      encoder_(link, fenced_signals_, session_, osd_, slots_, video_, parts.input_emitter) {

    session_.status().fence_with(&binder_.fio_queue());

    config_store_[0] = std::make_unique<svc::ConfigSnapshot>();
    config_store_[1] = std::make_unique<svc::ConfigSnapshot>();
    if (parts.stdout_routing) enable_stdout_routing();

    owed_mount_.bind(mount_status_cell_, &link_rx_);
    for (OwedSaveBind& owed : owed_save_)
        owed.bind(save_extent_cell_, &link_rx_);
}

LinkSession::~LinkSession() = default;

void LinkSession::set_pcm_feeder(PcmRingFeeder* feeder) noexcept {
    binder_.set_pcm_feeder(feeder);
    terms_.feeder = infra::opt_ref_of(feeder);
}

void LinkSession::attach_prefetch(svc::ChdPrefetch& pf) noexcept {
    prefetch_ = infra::OptRef{pf};
    terms_.prefetch = infra::OptRef<svc::ChdPrefetch>{pf};
}

void LinkSession::attach_discs(svc::DiscReadService& d) noexcept {
    discs_ = infra::OptRef{d};
    terms_.discs = infra::OptRef<svc::DiscReadService>{d};
}

void LinkSession::attach_mailbox(MailboxRelay& r) noexcept {
    mailbox_ = infra::OptRef{r};
    terms_.mailbox = infra::OptRef<cores::IMailboxPort>{r};
}

std::string_view LinkSession::conf_str_name() const noexcept { return bindings_.core_name(); }

bool LinkSession::core_supports_mgl() const noexcept {

    if (session_.type() == proto::CoreType::SharpMz) return false;
    const cores::CoreProfile& p = cores::profile_for(conf_str_name());
    return !p.is_front_end && !p.suppresses_mgl;
}

void LinkSession::reset_video_geometry() { video_.reset_geometry(core_edge_seq_); }

void LinkSession::arm_video_prelude() { video_.arm_prelude(); }

void LinkSession::publish_core_identity() { publish_identity(); }

bool LinkSession::declares_turbo() const noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return bindings_.declares_turbo();
}
void LinkSession::publish_identity() {

    const std::string_view name = bindings_.core_name();
    const std::string_view eff = effective_name(name, bindings_.facts());

    svc::JoyPlan joy{};
    std::string_view j_names{};
    if (const auto& bound = bindings_.identity(); bound.has_value()) {

        const cores::ButtonOverride ovr =
            current_.has_value() ? current_->button_override() : cores::ButtonOverride{};
        const svc::ButtonLists lists = svc::choose_button_lists(
            ovr.names, ovr.defaults, bound->j.view(), bound->jn.view(), bound->jp.view());
        joy = svc::build_joy_plan(lists.j, lists.jn, lists.jp,
                                  config() != nullptr && config()->gamepad_defaults != 0);
        j_names = lists.j;
    }

    identity_.publish(
        eff, name, RememberedStem::of(name, bindings_.facts()), joy, j_names,
        cores::profile_for(name).is_front_end,
        current_.has_value() && current_->profile().suppress_analog_followup,
        current_.has_value() ? current_->profile().analog_reshape
                             : infra::OptRef<const svc::IAnalogReshape>{},
        cores::profile_for(name).cue_browse_dir,
        infra::OptRef<const cores::CheatLookup>{cores::profile_for(name).cheats},
        cores::profile_for(name).slots,
        game_id_of(bindings_.facts(), config() != nullptr && config()->log_file_entry != 0),
        bindings_.facts().setname_same_dir, cores::profile_for(name).image_rows_no_zip,
        cores::profile_for(name).host.pause);
}

static constexpr EmitSite kAdviseSite{ERR_SITE()};

void LinkSession::push(const Event::SessionAdvisory& a) { machine_.advise(a, kAdviseSite); }
void LinkSession::push(const Event::InfoRequest& a) { machine_.advise(a, kAdviseSite); }
void LinkSession::push(const Event::SdActivity& a) { machine_.advise(a, kAdviseSite); }
void LinkSession::push(const Event::ProgressUpdate& a) { machine_.advise(a, kAdviseSite); }

void LinkSession::sleep_ms(unsigned ms) {
    if (ops_.sleeper) {
        ops_.sleeper->sleep_ms(ms);
        return;
    }
    ::usleep(static_cast<::useconds_t>(ms) * 1000u);
}

void LinkSession::publish_fabric_() { fabric_.refresh(fabric_cell_, clock().now().count()); }

LinkSession::RoundEntry LinkSession::poll(bool tick) {

    const bool live_at_entry = session_live();

    if (hold_wire_(tick)) return RoundEntry{.live = live_at_entry};

    if (tick && live_at_entry) {

        sampler_.service(ftx_open_.has_value() || stage_open_.has_value());
        const hal::SpiSample& g = sampler_.level();

        core_ready_ = g.ready;
        if (core_ready_) {
            const std::uint16_t but = g.buttons;

            if (!video_.prelude_running()) {

                (void)video_.update_but_sw(link_, but, osd_.has_focus(), &binder_.fio_queue());

                if (!osd_rearmed_) {

                    if (!osd_.lit() || osd_.set_show(osd_.show()).has_value()) {
                        osd_rearmed_ = true;
                    }
                }
            }
        }
    }

    if (tick) publish_fabric_();

    park_.poll(quiesce_);
    return RoundEntry{.live = live_at_entry};
}

void LinkSession::tick(RoundEntry entry) {
    if (wire_held()) {
        publish_diag();
        return;
    }
    const bool live_at_entry = entry.live;

    drain_write_verdicts_();
    if (window_jobs_) {
        cores::IWindowSave* role = current_.has_value() ? current_->window_save() : nullptr;
        if (const auto act = window_jobs_->service(role)) publish_ftx_level_(*act, true);
    }

    answer_reboot_();

    if (live_at_entry && session_live() && core_ready_) {
        video_.on_rt_round(link_, clock().now().count(), &binder_.fio_queue());
    }

    if (const cores::ICdDiagnostics* cd =
            current_.has_value() ? current_->cd_diagnostics() : nullptr;
        cd != nullptr) {
        if (const std::optional<svc::DiscCounters> dc = cd->disc_counters(); dc.has_value()) {
            disc_sync_decompress_.store(dc->sync_decompress, std::memory_order_relaxed);
            disc_park_timeouts_.store(dc->park_timeouts, std::memory_order_relaxed);
            disc_prefetch_refusals_.store(dc->prefetch_refusals, std::memory_order_relaxed);
        }

        if (const auto f = cd->cd_flow(); f.has_value()) {
            cd_flow_waits_ = f->waits;
            cd_cdda_sectors_ = f->cdda_sectors;
            cd_data_sectors_ = f->data_sectors;
            cd_idle_ticks_ = f->idle_ticks;
            cd_gated_ticks_ = f->gated_ticks;
            cd_drive_state_ = f->drive_state;
            cd_drive_track_ = f->drive_track;
            cd_drive_lba_ = f->drive_lba;
            cd_drive_audio_lba_ = f->drive_audio_lba;
            cd_drive_is_data_ = f->drive_is_data;
            cd_substitutes_ = f->substitutes;
            cd_subcode_substitutes_ = f->subcode_substitutes;
            cd_not_resident_ = f->not_resident;
            cd_busy_ticks_ = f->busy_ticks;
            cd_egress_abandons_ = f->egress_abandons;
        }
    }

    if (park_.asked()) {

        if (binder_.bridge_windows_live()) binder_.retry_release();
        const bool torn_down = dropped_gen_ == park_.gen() && encoder_.holds() != holds_at_drop_;

        park_.step(quiesce_, !torn_down || binder_.bridge_windows_live() || drop_owed_);
    }

    publish_diag();
}

bool LinkSession::push_save_bytes_(proto::SaveKind kind, proto::SaveStatus st, proto::RxSlabId path,
                                   proto::RxSlabId data, CorrelationTag tag) noexcept {
    return out_.push(proto::LinkEvent::SaveBytes{
        .which = kind, .status = st, .rel_path = path, .payload = data, .tag = tag});
}

void LinkSession::answer_save_ask_(const proto::LinkOp::SaveAsk& ask) noexcept {
    const proto::SaveKind kind = ask.which;
    const CorrelationTag tag = ask.tag;

    cores::IDipSwitches* sw = nullptr;
    cores::IConfigSlots* cs = nullptr;
    FixedStr<kConfigRelCap, StrFit::Reject> rel;
    std::span<const std::byte> payload;

    std::array<std::uint8_t, 8> dips{};
    std::byte pack[kSlotPackBytes]{};

    switch (kind) {
        case proto::SaveKind::Dips: {
            sw = current_.has_value() ? current_->dip_switches() : nullptr;
            if (sw == nullptr) {
                return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag);
            }
            if (!sw->dips_dirty()) {
                return (void)push_save_bytes_(kind, proto::SaveStatus::Nothing, {}, {}, tag);
            }
            rel = config_rel("config/dips/", sw->dip_file_name());
            dips = sw->dip_bytes();
            payload = std::as_bytes(std::span<const std::uint8_t>(dips));
            break;
        }
        case proto::SaveKind::Slot:
        case proto::SaveKind::SlotNames: {
            cs = current_.has_value() ? current_->config_slots() : nullptr;
            if (cs == nullptr) {

                if (!current_.has_value()) publish_config_slots();
                return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag);
            }
            const auto fname = cs->file_name(cores::ConfigSlot{ask.slot});
            rel = config_rel("config/", fname.view());

            payload = kind == proto::SaveKind::Slot
                          ? cs->snapshot()
                          : std::span<const std::byte>(pack, pack_slot_names(*cs, pack));
            break;
        }
        case proto::SaveKind::Nvram:
            return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag);
    }

    if (rel.empty()) {
        return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag);
    }
    const auto path_id =
        out_.intern<proto::LinkEvent::SaveBytes>(infra::text_bytes<const std::uint8_t>(rel.view()));
    if (!path_id) {
        return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag);
    }
    proto::RxSlabId data_id{};
    if (!payload.empty()) {
        const auto id =
            out_.intern<proto::LinkEvent::SaveBytes>(infra::byte_cast<const std::uint8_t>(payload));
        if (!id) {
            return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag);
        }
        data_id = *id;
    }
    const bool accepted = push_save_bytes_(kind, proto::SaveStatus::Bytes, *path_id, data_id, tag);
    if (accepted && kind == proto::SaveKind::Dips && sw != nullptr) {

        sw->mark_dips_saved();
        ++dip_saves_;
    }
}

void LinkSession::deliver(const proto::LinkOp& op, const ILinkTxReader& inbox,
                          bool windows_closed) noexcept {

    const EncoderRoles roles{
        .saves = infra::OptRef<ISaveFlush>{*this},
        .cheats = infra::OptRef<ICheatApply>{cheats_},
        .cheat_call = {.core = infra::opt_ref_of(core_ptr_()),
                       .fio_held = ftx_open_.has_value() || stage_open_.has_value()},
        .acts = infra::OptRef<ICoreOptionActs>{*this}};
    const LinkOpCtx ctx{.inbox = infra::OptRef<const ILinkTxReader>{inbox},
                        .windows_closed = windows_closed,
                        .live = session_live_(),
                        .fence = infra::OptRef<proto::IResetFence>{binder_.fio_queue()},
                        .roles = infra::OptRef<const EncoderRoles>{roles}};
    const auto kind = infra::kind_of(op);
    ILinkEncoder::Outcome r = ILinkEncoder::Outcome::Dropped;
    if (admitted(window_for(kind), ctx)) {
        r = target_for(kind) == OpTarget::Wire
                ? infra::dispatch<LinkOpRoutes<OpTarget::Wire>>(op, encoder_, ctx)
                : infra::dispatch<LinkOpRoutes<OpTarget::Session>>(op, *this, ctx);
    } else if (const auto ftx = infra::as<proto::LinkOp::FileTx>(op); ftx) {

        if (ftx->phase == proto::LinkOp::FileTxPhase::WindowOpen) {
            publish_ftx_level_(ftx->act, false);
        } else if (ftx->phase != proto::LinkOp::FileTxPhase::WindowClose) {
            publish_stream_(ftx->act, false);
        }
    }

    if (const auto sp = infra::as<proto::LinkOp::StagePayload>(op);
        sp && sp->act != 0 && !copy_ds_) {
        publish_ftx_level_(sp->act, r != ILinkEncoder::Outcome::Dropped, true);
    }
    if (r == ILinkEncoder::Outcome::Dropped) {
        ++link_op_drops_;
    } else {
        ++link_ops_encoded_;
        if (r == ILinkEncoder::Outcome::EncodedStatusChange) {
            publish_status_word();
        }
    }
}

void LinkSession::take_bound_config_() noexcept {
    if (config_reader_.take_if_changed(config_cell_, spare_config_())) {
        if (auto r = apply(live_config_(), spare_config_()); r) {
            promote_spare_();
        } else {
            push(Event::SessionAdvisory{.why = r.error().code});
        }
    } else if (config_reader_.seen() != 0 && config_cell_.generation() == config_reader_.seen()) {
        if (auto r = apply(live_config_(), live_config_()); !r) {
            push(Event::SessionAdvisory{.why = r.error().code});
        }
    } else {

        ++link_op_drops_;
    }
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindDecoders& a,
                                      const LinkOpCtx&) noexcept {
    switch (a.table) {
        case proto::LinkOp::DecoderTable::Census: {
            std::array<reactor::LinkDecoderDecl, kMaxServices> rows{};

            const bool dead = machine_.state() == SessionState::Failed;
            if (auto r = rebind_census(dead ? std::span<const reactor::LinkDecoderDecl>{}
                                            : census_rows_(rows));
                !r) {
                return ILinkEncoder::Outcome::Dropped;
            }
            return ILinkEncoder::Outcome::Encoded;
        }
        case proto::LinkOp::DecoderTable::PreSession:
            if (auto r = bind_pre_session_(a.gen); !r) {
                return ILinkEncoder::Outcome::Dropped;
            }
            return ILinkEncoder::Outcome::Encoded;
    }
    return ILinkEncoder::Outcome::Dropped;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::DropCore& a, const LinkOpCtx&) noexcept {
    start_.end(a);

    park_.poll(quiesce_);
    if (park_.asked()) {
        dropped_gen_ = park_.gen();
        holds_at_drop_ = encoder_.holds();
    }
    ++core_edge_seq_;
    if (mailbox_) mailbox_->forget();
    RtRelaxScope scope(ops_);
    if (const auto r = detach(); !r) {
        last_error_ = r.error();
        machine_.transition(SessionState::Failed);
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::AnnounceMount& a,
                                      const LinkOpCtx&) noexcept {
    const proto::SlotIndex si = a.slot;
    if (si.v >= proto::kBlockSlots) {
        return ILinkEncoder::Outcome::Dropped;
    }

    if (auto r = slots_.notify_mount(link_, a.loaded); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindSlot& a,
                                      const LinkOpCtx& ctx) noexcept {
    if (a.bind == proto::LinkOp::SlotBind::Attach || a.bind == proto::LinkOp::SlotBind::Detach) {
        return apply_save_bind_(a, ctx.inbox);
    }
    const proto::SlotIndex si = a.slot;
    if (si.v >= proto::kBlockSlots) {
        return ILinkEncoder::Outcome::Dropped;
    }

    if (current_.has_value()) {
        apply_bind_slot_roles_(proto::SlotIndex{current_->profile().staging.disc_slot.v},
                               current_slot_roles_());
    }

    const proto::LinkOp::SlotBind kind = a.bind;
    if (kind == proto::LinkOp::SlotBind::Mount && a.path == proto::FileId{}) {
        return ILinkEncoder::Outcome::Encoded;
    }
    proto::FileSize size{};
    if (a.path != proto::FileId{}) {
        if (!ctx.inbox) return ILinkEncoder::Outcome::Dropped;
        const InternedFile* const file = ctx.inbox->file(a.path);
        if (file == nullptr) return ILinkEncoder::Outcome::Dropped;
        size = proto::FileSize{file->size_bytes};
    }
    const proto::PathId path_id{a.path.v};
    if (!a.bracketed) {
        if (a.sends_index && kind != proto::LinkOp::SlotBind::MountCd) {
            (void)binder_.fio_queue().flush();
            if (auto r = proto::DownloadSession::set_index(link_, proto::IoIndex{a.index_word}); !r)
                return ILinkEncoder::Outcome::Dropped;
        }
        if (auto r = apply_bind_slot_(si, kind, size, path_id); !r) {
            return ILinkEncoder::Outcome::Dropped;
        }
        return ILinkEncoder::Outcome::Encoded;
    }

    cores::IStagingCore* const sd = current_.has_value() ? current_->staging_core() : nullptr;
    if (sd == nullptr) return ILinkEncoder::Outcome::Dropped;
    auto frame = proto::ImageBracket::open(sd->image_sink(), current_->profile().staging.save_dest);
    if (!frame) return ILinkEncoder::Outcome::Dropped;
    const auto bound = apply_bind_slot_(si, kind, size, path_id);
    const auto closed = frame->end();
    if (!bound || !closed) return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::FileTx& a,
                                      const LinkOpCtx& ctx) noexcept {
    using Phase = proto::LinkOp::FileTxPhase;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    if (!current_.has_value() || !ctx.inbox || machine_.state() != SessionState::Running ||
        ((window ? current_->window_load() : current_->stream_load()) == nullptr &&
         a.phase != Phase::Whole)) {
        if (a.phase != Phase::Whole) ++ftx_piece_drops_;

        if (a.phase == Phase::WindowOpen) publish_ftx_level_(a.act, false);
        if (!window) publish_stream_(a.act, false);
        return ILinkEncoder::Outcome::Dropped;
    }
    const InternedFile* const file = ctx.inbox->file(a.file);
    RtRelaxScope scope(ops_);
    cut_stage_();
    if (a.phase == Phase::Whole || a.phase == Phase::Close) arm_open_save_(file, ctx.inbox);

    bool ok = false;
    if (window || current_->stream_load() != nullptr) {
        ok = stream_file_tx_(a, file);
    } else if (file != nullptr && !file->bytes.empty()) {
        SpanFile f{file->bytes};
        ok = perform_file_tx_(f, file->path.view(), a.wire_index, file->edge).has_value();
    }
    open_save_.reset();
    if (!window && !(ok && link_.posted())) publish_stream_(a.act, ok);
    return ok ? ILinkEncoder::Outcome::Encoded : ILinkEncoder::Outcome::Dropped;
}

void LinkSession::arm_open_save_(const InternedFile* file,
                                 infra::OptRef<const ILinkTxReader> inbox) noexcept {
    open_save_.reset();
    if (file == nullptr || !inbox || file->save == proto::FileId{}) return;
    const InternedFile* const save = inbox->file(file->save);
    if (save == nullptr) {
        ++open_save_refusals_;
        return;
    }
    open_save_ =
        OpenSave{.path = proto::PathId{file->save.v}, .size = proto::FileSize{save->size_bytes}};
}

void LinkSession::announce_open_save_() noexcept {
    if (!open_save_) return;
    const OpenSave s = *open_save_;
    open_save_.reset();
    if (!apply_bind_slot_(proto::SlotIndex{0}, proto::LinkOp::SlotBind::Mount, s.size, s.path)) {
        ++open_save_refusals_;
        return;
    }
    ++open_saves_mounted_;
}

bool LinkSession::stream_file_tx_(const proto::LinkOp::FileTx& a, const InternedFile* file) {
    using Phase = proto::LinkOp::FileTxPhase;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    const bool opens =
        a.phase == Phase::Whole || a.phase == Phase::Open || a.phase == Phase::WindowOpen;
    const bool closes =
        a.phase == Phase::Whole || a.phase == Phase::Close || a.phase == Phase::WindowClose;
    if (opens) {
        if (ftx_open_) cut_file_tx_();
        if (file == nullptr || (!window && file->bytes.empty())) {
            if (a.phase != Phase::Whole) ++ftx_piece_drops_;
            if (window) publish_ftx_level_(a.act, false);
            return false;
        }
        const std::string_view path = file->path.view();
        const std::size_t dot = path.rfind('.');
        current_->set_pending_file_ext(dot == std::string_view::npos ? std::string_view{}
                                                                     : path.substr(dot));
        current_->set_pending_file_path(path);

        if (!window) {
            binder_.file_progress().arm(*this,
                                        a.phase == Phase::Whole ? file->bytes.size() : a.total);
        }
        cores::IStreamLoad& load = window ? *current_->window_load() : *current_->stream_load();
        auto b = cores::StreamLoadBracket::open(load, current_->image_sink(),
                                                proto::IoIndex{a.wire_index}, window ? a.total : 0);
        if (!b) {
            if (window) {
                publish_ftx_level_(a.act, false);
            } else {
                binder_.file_progress().disarm();
            }
            return false;
        }
        ftx_open_.emplace(std::move(*b));
        if (window) {

            cores::IWindowSave* role = current_->window_save();
            if (role != nullptr && window_jobs_ &&
                window_jobs_->hold_open(*role, proto::IoIndex{a.wire_index}, a.act)) {
                return true;
            }
            publish_ftx_level_(a.act, true);
            return true;
        }
    } else if (!ftx_open_) {
        ++ftx_piece_drops_;
        return false;
    }

    if ((a.phase == Phase::Close || a.phase == Phase::WindowClose) && file == nullptr) {
        cut_file_tx_();
        return true;
    }
    if (!window) {

        if (file == nullptr) {
            ++ftx_piece_drops_;
            cut_file_tx_();
            return false;
        }
        const auto taken =
            file->bytes.empty() ? Ex<std::size_t>{0u} : ftx_open_->write_taken(file->bytes);
        if (!taken) {
            ++ftx_piece_drops_;
            cut_file_tx_();
            return false;
        }

        if (link_.posted()) {
            ++ftx_holds_;
            open_save_.reset();
            publish_stream_(a.act, true, static_cast<std::uint32_t>(*taken));
            return true;
        }
    }
    if (!closes) return true;

    before_close();
    const std::uint64_t total = a.phase == Phase::Whole ? file->bytes.size() : a.total;
    const bool ok = ftx_open_->close(total, file->crc).has_value();
    ftx_open_.reset();
    if (!window) binder_.file_progress().disarm();
    if (!ok) return false;

    if (!window || current_->stream_load() != nullptr)
        after_file_tx_(a.wire_index, file->path.view(), file->edge);
    return true;
}

ILinkEncoder::Outcome LinkSession::stage_copy_(const proto::LinkOp::StagePayload& a,
                                               std::span<const std::uint8_t> bytes) noexcept {
    cut_stage_();
    const std::size_t n = bytes.size() / 2u;
    if (copy_ds_ || proto::sets_region(a) || a.copy_word > n || n > kCopyWordsMax) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::array<std::uint16_t, kCopyWordsMax> w{};
    for (std::size_t i = 0; i < n; ++i)
        w[i] = static_cast<std::uint16_t>(bytes[2u * i] | (bytes[2u * i + 1u] << 8));
    (void)binder_.fio_queue().flush();
    auto ds = proto::DownloadSession::begin(link_, a.dest);
    if (!ds) return ILinkEncoder::Outcome::Dropped;
    if (auto r = ds->post(std::span(w).first(a.copy_word)); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    copy_tail_n_ = static_cast<std::uint8_t>(n - a.copy_word);
    for (std::size_t i = 0; i < copy_tail_n_; ++i)
        copy_tail_[i] = w[a.copy_word + i];
    copy_act_ = a.act;
    copy_ds_.emplace(std::move(*ds));
    return ILinkEncoder::Outcome::Encoded;
}

bool LinkSession::hold_wire_(bool tick) noexcept {
    if (!wire_held()) return false;
    if (tick) publish_fabric_();
    park_.poll(quiesce_);
    if (!copy_ds_) {

        if (park_.asked()) {
            link_.abandon_post();
            ++ftx_hold_abandons_;
            return false;
        }
        const auto done = link_.posted_done();
        if (done && !*done) {
            ++ftx_held_rounds_;
            return true;
        }
        return false;
    }
    if (park_.asked()) {
        copy_ds_->abandon();
        end_copy_(false);
        return false;
    }
    const auto done = copy_ds_->settle(std::span(copy_tail_).first(copy_tail_n_));
    if (done && !*done) {
        ++copy_rounds_;
        return true;
    }
    const bool ok = done.has_value() && copy_ds_->end().has_value();
    end_copy_(ok);
    return false;
}

void LinkSession::end_copy_(bool ok) noexcept {
    copy_ds_.reset();
    if (copy_act_ != 0) publish_ftx_level_(copy_act_, ok, true);
    if (!ok) ++link_op_drops_;
    copy_act_ = 0;
    copy_tail_n_ = 0;
}

void LinkSession::publish_ftx_level_(std::uint32_t act, bool open, bool payload) noexcept {
    ftx_level_scratch_.act = act;
    ftx_level_scratch_.open = open;
    ftx_level_scratch_.payload = payload;
    ftx_level_cell_.publish(ftx_level_scratch_);
}

void LinkSession::publish_stream_(std::uint32_t act, bool served,
                                  std::optional<std::uint32_t> held) noexcept {
    if (act == 0) return;
    ftx_level_scratch_.stream_act = act;
    ftx_level_scratch_.stream_served = served;
    if (held) {
        ftx_level_scratch_.held_act = act;
        ftx_level_scratch_.held_bytes = *held;
    }
    ftx_level_cell_.publish(ftx_level_scratch_);
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::LoadFacts& a,
                                      const LinkOpCtx& ctx) noexcept {
    if (!current_.has_value() || !ctx.inbox || machine_.state() != SessionState::Running) {
        ++load_facts_refusals_;
        return ILinkEncoder::Outcome::Dropped;
    }

    RtRelaxScope scope(ops_);
    if (!current_->apply_load_facts(ctx.inbox->bytes(a.facts))) {
        ++load_facts_refusals_;
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::MailboxWrite& a,
                                      const LinkOpCtx&) noexcept {
    if (!current_.has_value() || machine_.state() != SessionState::Running) {
        ++mailbox_write_drops_;
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
    if (mailbox_) mailbox_->bind(a.poll, a.gen);
    if (!queued) {
        ++mailbox_write_drops_;
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

bool LinkSession::save_pull_fenced_() noexcept {
    if ((!ftx_open_ && !stage_open_) || current_->save_upload() == nullptr) return false;
    ++save_pulls_fenced_;
    return true;
}

void LinkSession::cut_file_tx_() noexcept {
    if (!ftx_open_) return;
    ftx_open_.reset();
    ++ftx_cuts_;
    binder_.file_progress().disarm();
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::AdoptCore& a,
                                      const LinkOpCtx&) noexcept {
    const auto made = adopt_(a);

    if (!out_.push(
            proto::LinkEvent::CoreMade{.made = made.has_value(),
                                       .restore_owed = made.has_value() && current_.has_value() &&
                                                       current_->config_slots() != nullptr,
                                       .cheat_records = made.has_value() && current_.has_value() &&
                                                        current_->cheat_records() != nullptr,
                                       .bind_gen = bindings_.generation().v,
                                       .err = made.has_value() ? Errc{} : made.error().code}))
        ++core_made_drops_;
    if (!made) {

        (void)binder_.unbind_session();
        last_error_ = made.error();
        machine_.transition(SessionState::Failed);
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::SessionUp& a,
                                      const LinkOpCtx&) noexcept {
    RtRelaxScope scope(ops_);
    if (const auto r = perform_session_up_(a); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindFacts& a,
                                      const LinkOpCtx& ctx) noexcept {
    bindings_.bind_facts(a, inbox_ptr(ctx.inbox));
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindIdentity& a,
                                      const LinkOpCtx& ctx) noexcept {
    bindings_.bind_identity(a, inbox_ptr(ctx.inbox));
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindDoorbells& a,
                                      const LinkOpCtx& ctx) noexcept {
    bindings_.bind_doorbells(a, inbox_ptr(ctx.inbox));
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindConfig& a,
                                      const LinkOpCtx& ctx) noexcept {
    if (a.ini_refused) {

        push(Event::SessionAdvisory{.why = a.parse_err});
        if (auto r = apply(live_config_(), live_config_()); !r) {
            push(Event::SessionAdvisory{.why = r.error().code});
        }
    } else {
        take_bound_config_();
    }
    video_.set_key_map(a.conf);
    if (a.waitmount_exhausted) {
        push(Event::SessionAdvisory{.why = Errc::timeout});
    }
    if (a.cfg != proto::TxSlabId{} && ctx.inbox && session_.type() == proto::CoreType::EightBit) {
        const auto bytes = ctx.inbox->bytes(a.cfg);
        if (bytes.size() == 16) {
            auto& st = session_.status();
            for (unsigned bit = 0; bit < proto::StatusRegister::kBits; ++bit) {
                const auto byte_v = bytes[bit / 8];
                st.set_bit(proto::StatusBit{static_cast<std::uint8_t>(bit)},
                           ((byte_v >> (bit % 8)) & 1u) != 0);
            }
        }
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindUart& a,
                                      const LinkOpCtx& ctx) noexcept {
    bindings_.bind_uart(a, inbox_ptr(ctx.inbox));

    if (a.probe_usb_ser) uart_.latch_usb_ser(a.usb_ser);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindSlotConfig& a,
                                      const LinkOpCtx& ctx) noexcept {
    cores::IConfigSlots* cs = current_.has_value() ? current_->config_slots() : nullptr;
    if (cs == nullptr || !ctx.inbox) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::span<const std::byte> blob{};
    if (a.file != proto::FileId{}) {
        const InternedFile* const file = ctx.inbox->file(a.file);
        if (file == nullptr) {
            return ILinkEncoder::Outcome::Dropped;
        }
        blob = std::as_bytes(std::span<const std::uint8_t>(file->bytes));
    }
    if (auto r = cs->restore(cores::ConfigBlob{blob}); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    ++config_loads_;
    publish_option_table();
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindMount& a,
                                      const LinkOpCtx& ctx) noexcept {
    const auto image = SessionBindings::decode_mount(a, inbox_ptr(ctx.inbox));
    if (!image) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return perform_bound_mount_(a, *image);
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::BindSlotPreviews& a,
                                      const LinkOpCtx& ctx) noexcept {
    cores::IConfigSlots* cs = current_.has_value() ? current_->config_slots() : nullptr;
    if (cs == nullptr || !ctx.inbox) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto count = std::min<std::uint8_t>(a.count, cores::kMaxConfigSlots);
    config_slot_scratch_ = ConfigSlotTable{};
    config_slot_scratch_.count = count;
    for (std::uint8_t i = 0; i < count; ++i) {
        const InternedFile* const file = ctx.inbox->file(InternedFile::config_id(i));
        std::span<const std::byte> blob{};
        if (file != nullptr && !file->bytes.empty()) {
            blob = std::as_bytes(std::span<const std::uint8_t>(file->bytes));
            config_slot_scratch_.exists |= static_cast<std::uint16_t>(1u << i);
        }
        config_slot_scratch_.preview[i] = cs->preview(cores::ConfigBlob{blob});
    }
    config_slot_cell_.publish(config_slot_scratch_);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::RebootNow& a,
                                      const LinkOpCtx&) noexcept {
    if (a.quiesce) {

        fenced_signals_.set_core_reset(true);
        rebooting_ = true;
        reboot_answered_ = false;
        reboot_seq_ = a.seq;
        start_.end(a);

        if (machine_.state() != SessionState::Failed) {
            machine_.transition(SessionState::Failed);
        }
        return ILinkEncoder::Outcome::Encoded;
    }
    perform_reboot_();
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::ApplyCore& a,
                                      const LinkOpCtx&) noexcept {
    apply_core_(a);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::AbortSwitch& a,
                                      const LinkOpCtx&) noexcept {

    if (park_.engaged() && a.gen.v == static_cast<std::uint16_t>(park_.gen())) {
        park_.withdraw();
        push(Event::SessionAdvisory{.why = a.why});
        return ILinkEncoder::Outcome::Encoded;
    }
    if (!start_.on() || a.gen != start_.gen()) {
        ++stale_owner_steps_;
        return ILinkEncoder::Outcome::Encoded;
    }
    start_.end(a);

    session_.refuse();
    if (machine_.state() == SessionState::Boot) machine_.transition(SessionState::Running);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::StageMount& a,
                                      const LinkOpCtx& ctx) noexcept {
    cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;
    const InternedFile* const file =
        (a.path == proto::FileId{} || !ctx.inbox) ? nullptr : ctx.inbox->file(a.path);
    const std::string_view path = file == nullptr ? std::string_view{} : file->path.view();

    const bool parked = !prefetch_;
    const std::int64_t quiesce_ns = parked ? 0 : clock().now().count() + kQuiesceBudgetNs;

    owed_mount_.arm(a.act_gen, mount_body_(), path, parked, quiesce_ns);
    if (cd == nullptr || (a.path != proto::FileId{} && file == nullptr)) {
        owed_mount_.answer(cores::MountVerdict::Failed, mount_body_());
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!parked) {
        prefetch_->request_park();
        return ILinkEncoder::Outcome::Encoded;
    }
    const cores::MountState ms = cd->mount_disc(path);
    if (ms == cores::MountState::Pending) {

        return ILinkEncoder::Outcome::Encoded;
    }
    after_mount_answer_();
    owed_mount_.answer(ms == cores::MountState::Done ? cores::MountVerdict::Done
                                                     : cores::MountVerdict::Failed,
                       mount_body_());
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::StageCheats& a,
                                      const LinkOpCtx& ctx) noexcept {
    if (!current_.has_value()) return ILinkEncoder::Outcome::Dropped;
    const InternedFile* const file = !ctx.inbox ? nullptr : ctx.inbox->file(a.path);
    if (file == nullptr) return ILinkEncoder::Outcome::Dropped;
    cheats_.content_loaded(core_ptr_(), TxDigest::Kind::Mount, file->path.view(), 0, a.same_game);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::SetWideIndex& a,
                                      const LinkOpCtx&) noexcept {
    cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;

    if (cd == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    (void)binder_.fio_queue().flush();
    if (auto r = proto::DownloadSession::set_index(link_, a.dest); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::StagePayload& a,
                                      const LinkOpCtx& ctx) noexcept {
    using Phase = proto::LinkOp::StagePhase;
    const bool opens = a.phase == Phase::Whole || a.phase == Phase::Open;
    const bool closes = a.phase == Phase::Whole || a.phase == Phase::Close;
    cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;

    std::optional<RtRelaxScope> relaxed;
    if (a.copy_word == 0 || a.phase != Phase::Whole) relaxed.emplace(ops_);

    if (!current_.has_value() || (cd == nullptr && proto::sets_region(a))) {
        return drop_stage_();
    }
    const InternedFile* file = nullptr;
    std::span<const std::uint8_t> bytes{};
    if (a.payload != proto::FileId{}) {
        file = ctx.inbox ? ctx.inbox->file(a.payload) : nullptr;
        if (file == nullptr) return drop_stage_();
        bytes = file->bytes;
    }

    const bool load_phase = a.phase == Phase::Whole && start_.on();
    if (!load_phase && !stage_fits_round_(bytes.size())) return drop_stage_();
    if (a.copy_word != 0) {
        return a.phase == Phase::Whole ? stage_copy_(a, bytes) : drop_stage_();
    }

    if (a.phase == Phase::Close && file == nullptr) {
        if (!stage_open_) return drop_stage_();
        cut_stage_();
        return ILinkEncoder::Outcome::Encoded;
    }
    if (opens) {
        cut_stage_();
        cut_file_tx_();
        proto::SessionParams params{};
        if (file != nullptr) {
            params.ext = file->ext.view();
            params.aux = proto::AuxValue{file->aux};
        }
        proto::IImageSink& sink = cd != nullptr ? cd->image_sink() : current_->image_sink();
        auto ds = proto::ImageBracket::open(sink, a.dest, params);
        if (!ds) return ILinkEncoder::Outcome::Dropped;
        stage_open_.emplace(std::move(*ds));
        stage_tick_.bind(a.progress ? this : nullptr);
        stage_tick_.begin(file != nullptr ? file->size_bytes : 0);
    } else if (!stage_open_) {
        return drop_stage_();
    }
    if (!bytes.empty() && !drain_payload_(bytes, a.chunk, file->offset)) {
        cut_stage_();
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!closes) return ILinkEncoder::Outcome::Encoded;
    stage_tick_.finish();
    const bool ended = stage_open_->end().has_value();
    stage_open_.reset();
    if (!ended) return ILinkEncoder::Outcome::Dropped;
    if (proto::sets_region(a) && cd != nullptr) cd->set_region(a.region);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::drop_stage_() noexcept {
    ++stage_drops_;
    cut_stage_();
    return ILinkEncoder::Outcome::Dropped;
}

bool LinkSession::stage_fits_round_(std::size_t bytes) const noexcept {
    const std::size_t words = link_.width() == hal::Width::Word ? (bytes + 1u) / 2u : bytes;
    return words <= proto::tightest_load_budget_words();
}

void LinkSession::cut_stage_() noexcept {
    if (!stage_open_) return;
    stage_open_.reset();
    ++stage_cuts_;
    stage_tick_.finish();
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::StageReset&, const LinkOpCtx&) noexcept {
    cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;
    if (cd == nullptr) return ILinkEncoder::Outcome::Dropped;
    if (auto r = cd->deferred_reset(); !r) return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::CoreReset& a,
                                      const LinkOpCtx& ctx) noexcept {
    if (!current_.has_value()) return ILinkEncoder::Outcome::Dropped;
    const auto pulse = current_->reset(a.edge);
    if (!pulse) return ILinkEncoder::Outcome::Dropped;
    if (cores::IWindowSave* role = current_->window_save(); role != nullptr && window_jobs_) {
        (void)window_jobs_->arm_reset_edge(*role, a.edge);
    }
    if (!*pulse) return ILinkEncoder::Outcome::Encoded;
    const auto out = encoder_.on(proto::LinkOp::PulseOption{.bit = **pulse}, ctx);
    if (out == ILinkEncoder::Outcome::EncodedStatusChange) ++resets_pulsed_;
    return out;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::StageDiscPayload& a,
                                      const LinkOpCtx&) noexcept {
    cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;
    if (cd == nullptr) return ILinkEncoder::Outcome::Dropped;
    RtRelaxScope scope(ops_);
    if (auto r = cd->stage_disc_payload(a); !r) return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::SetVolume& a,
                                      const LinkOpCtx&) noexcept {

    video_.audio().set_volume(a.cmd, a.arg);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::on(const proto::LinkOp::SaveAsk& a, const LinkOpCtx&) noexcept {
    answer_save_ask_(a);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::misrouted(const proto::LinkOp&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    ++link_op_misrouted_;
    return ILinkEncoder::Outcome::Dropped;
}

static_assert(LinkOpSessionSink<LinkSession>);

Ex<void> LinkSession::arm_or_write_(std::string_view rel, std::span<const std::byte> bytes) {
    if (!vfs_) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    if (!writes_) return durable_write(*vfs_, rel, bytes);
    if (!writes_->arm(rel, bytes, WriteKind::Save)) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    return {};
}

void LinkSession::drain_write_verdicts_() {
    if (!writes_) return;
    while (const auto d = writes_->reap()) {

        switch (d->kind) {
            case WriteKind::Silent:
                break;
            case WriteKind::Save:

                if (!d->ok) {
                    ++save_upload_errs_;
                    log_edge(LogRec::Refusal{Errc::io, ERR_SITE()});
                }
                break;
        }
    }
}

constexpr unsigned kSaveSettleTries = 2000;

void LinkSession::settle_save_lane_() {
    if (!writes_) return;

    for (unsigned i = 0; i < kSaveSettleTries && writes_->in_flight() != 0; ++i) {
        drain_write_verdicts_();
        if (writes_->in_flight() == 0) break;
        sleep_ms(1);
    }
}

void LinkSession::settle_writes() noexcept {
    if (!writes_) return;
    writes_->drain_on_caller();
    drain_write_verdicts_();
}

template <class A>
void LinkSession::log_edge(const A& alt) noexcept {
    (void)log_lane_.push(alt, log_now_ns());
}

Ex<std::optional<UiRequest::LoadCore>> LinkSession::boot(std::span<const char* const> argv) {

    if (machine_.state() != SessionState::Boot) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    const char* rbf = (argv.size() > 1 && argv[1] != nullptr) ? argv[1] : "";
    const char* xml = (argv.size() > 2) ? argv[2] : nullptr;
    if (xml != nullptr && xml[0] == '\0') xml = nullptr;

    if (vfs_) publish_fabric_();

    LoadRequest req{};
    if (rbf[0] != '\0') {
        if (!req.path.assign(rbf)) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
        req.kind = xml_kind_of(rbf);
    }
    if (xml != nullptr) {
        if (!req.xml.assign(xml)) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    }

    if (req.kind != XmlKind::Rbf) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(req.kind)});
    }
    arm_request(req);

    if (!req.path.empty()) {
        UiRequest::LoadCore sw{};
        sw.path = req.path;
        sw.xml = req.kind;

        fenced_signals_.set_core_reset(true);
        start_.arm_at_boot();
        return sw;
    }

    publish_fabric_();
    if (const auto done = adopt_programmed_core_(); !done) {
        machine_.transition(SessionState::Running);
        return std::unexpected(done.error());
    }

    if (auto r = bind_pre_session_(proto::BindGeneration{kBootGen}); !r) {
        machine_.transition(SessionState::Running);
        return std::unexpected(r.error());
    }
    start_.arm_at_boot();
    return std::nullopt;
}

Ex<void> LinkSession::load_core(const LoadRequest& req) {
    if (req.policy == ReloadPolicy::ReExec) return re_exec(req);

    return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
}

Ex<void> LinkSession::reset_core() {

    if (session_.type() != proto::CoreType::EightBit) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(session_.type())});
    }
    auto& st = session_.status();
    st.set_bit(proto::StatusBit{0}, true);
    if (auto r = session_.flush_status(); !r) return r;
    st.set_bit(proto::StatusBit{0}, false);
    return session_.flush_status();
}

void LinkSession::hold_for_reboot_() {

    ::sync();
    fenced_signals_.set_core_reset(true);
    if (machine_.state() != SessionState::Failed) machine_.transition(SessionState::Failed);
}

void LinkSession::apply_core_(const proto::LinkOp::ApplyCore& a) {
    const proto::BindGeneration gen = a.gen;

    const bool at_boot = gen.v == kBootGen;
    if (!at_boot && (!park_.parked() || gen.v != static_cast<std::uint16_t>(park_.gen()))) {
        ++stale_owner_steps_;
        return;
    }
    ++core_edge_seq_;

    info_poll_.arm(false, bindings_.generation());
    rumble_poll_.arm(false, bindings_.generation());
    f12_mod_poll_.arm(false, bindings_.generation());

    if (!at_boot) park_.release();

    machine_.adopt_tag(a.tag);

    start_.arm(a);

    if (!a.loaded) {
        refuse_start_(Error{Errc::core_load, ERR_SITE(), 0});
        machine_.transition(SessionState::Running);
        return;
    }

    publish_fabric_();
    if (const auto done = adopt_programmed_core_(); !done) {
        refuse_start_(done.error());
        machine_.transition(SessionState::Running);
        return;
    }

    bindings_.forget_core();
}

void LinkSession::refuse_start_(const Error& e) noexcept {
    last_error_ = e;
    if (!out_.push(proto::LinkEvent::StartRefused{.bind_gen = start_.gen(), .err = e.code})) {
        ++start_refused_drops_;
    }
}

Ex<bool> LinkSession::adopt_programmed_core_() {

    fenced_signals_.clear_gpo();
    fenced_signals_.set_core_reset(false);
    session_ = proto::CoreSession(link_, fenced_signals_, proto::MemSizeCookie{fabric_.cookie()},
                                  &binder_.fio_queue());
    return true;
}

Ex<void> LinkSession::perform_session_up_(const proto::LinkOp::SessionUp& op) {
    if (const auto r = raise_session_(); !r) {

        (void)binder_.unbind_session();
        refuse_start_(r.error());
        machine_.transition(SessionState::Failed);
        return r;
    }

    ever_running_ = true;
    start_.end(op);
    osd_.on_fabric_reprogrammed();
    machine_.transition(SessionState::Running);
    const bool front_end = cores::profile_for(conf_str_name()).is_front_end;
    cores::CoreInitContext init_ctx{link_, session_, *this, infra::opt_ref_of(core_ptr_())};
    (void)run_core_init(init_ctx, current_.has_value() ? current_->profile().init_additions
                                                       : std::span<const cores::CoreInitStep>{});
    publish_status_word();
    publish_dip_table();
    publish_config_slots();
    publish_option_table();
    osd_mask_decoder_.forget(front_end);
    info_poll_.arm(!front_end && bindings_.declares_info(), bindings_.generation());
    rumble_poll_.arm(!front_end, bindings_.generation());
    osd_rearmed_ = false;
    machine_.close(Event::CoreLoaded{.type = session_.type(),
                                     .mgl_capable = core_supports_mgl(),
                                     .front_end = front_end},
                   EmitSite{ERR_SITE()});
    log_edge(LogRec::CoreLoad{sat_u32((log_now_ns() - load_start_ns_) / 1'000'000u)});
    return {};
}

void LinkSession::answer_reboot_() noexcept {
    if (!rebooting_ || reboot_answered_ || !io_settled_()) return;

    reboot_answered_ = out_.push(proto::LinkEvent::RebootQuiesced{.seq = reboot_seq_});
}

bool LinkSession::io_settled_() const noexcept {
    return (!storage_life_ || storage_life_->quiesced()) &&
           (!writes_ || writes_->in_flight() == 0) && (!window_jobs_ || !window_jobs_->busy());
}

void LinkSession::perform_reboot_() {
    if (!rebooting_) {
        ++stale_owner_steps_;
        return;
    }

    release_bus();
}

Ex<ExitReboot> LinkSession::shutdown() {

    struct PublishOnExit {
        LinkSession* self;
        ~PublishOnExit() { self->publish_diag(); }
    } publish_on_exit{this};

    settle_save_lane_();
    const auto d = detach();

    if (drop_owed_) settle_retire_(RetireEnd::Exit);

    publish_fabric_();

    release_bus();

    if (fpga_unconfigured() && ever_running_) {
        std::fprintf(stderr, "mister: FPGA left unconfigured by a failed bitstream "
                             "program; the bridges stay down (raising them over an "
                             "unconfigured fabric is unsafe) and no userland write can "
                             "make this board bootable — rebooting now (cold)\n");
        hold_for_reboot_();
        return ExitReboot::Cold;
    }
    if (!d) return std::unexpected(d.error());
    return ExitReboot::None;
}

void LinkSession::release_bus() noexcept {

    link_.deselect();
    fenced_signals_.set_core_reset(false);
}

Ex<void> LinkSession::apply(const svc::ConfigSnapshot& previous, const svc::ConfigSnapshot& next) {

    (void)previous;
    if (stdout_armed_) {
        if (auto r = stdout_router_.route(next.debug); !r) return r;
    }

    return {};
}

static_assert(2u * sizeof(svc::ConfigSnapshot) < 192u * 1024u,
              "rt-rule: the config store is a FIXED two slots, sized by the option table "
              "alone. Nothing here grows with the number of cores, INI sections or loads, "
              "and two snapshots is the whole of it.");

svc::ConfigSnapshot& LinkSession::live_config_() noexcept { return *config_store_[config_live_]; }

svc::ConfigSnapshot& LinkSession::spare_config_() noexcept {
    return *config_store_[1u - config_live_];
}

void LinkSession::promote_spare_() noexcept {
    config_live_ = static_cast<std::uint8_t>(1u - config_live_);
}

void LinkSession::arm_request(const LoadRequest& req) {

    machine_.adopt_tag(kUncaused);

    load_start_ns_ = log_now_ns();
    pending_.arm(req);
}

Ex<void> LinkSession::rebind_census(std::span<const reactor::LinkDecoderDecl> rows) {
    return binder_.bind_census(rows, core_ptr_());
}

Ex<void> LinkSession::bind_pre_session_(proto::BindGeneration gen) {
    bindings_.bind_generation(gen);
    const std::array<reactor::LinkDecoderDecl, 2> rows{
        {{"link.identify", reactor::Cause::Tick, &identify_row_, 0, reactor::DeadlineClass::A,
          reactor::OsdBudget::Shared},
         {"link.conf_str", reactor::Cause::Tick, &conf_str_row_, 0, reactor::DeadlineClass::A,
          reactor::OsdBudget::Shared}}};
    return rebind_census(rows);
}

static_assert(requires(const proto::SpiBlockDecoder& d) { d.osd_claims(); });
static_assert(cores::CoreProfile{}.block_drain_budget == proto::SpiBlockDecoder::kDrainBudget,
              "the generic profile's drain bound is stock's four-iteration loop");

std::uint8_t LinkSession::issued_params(proto::user_io::PollShape s) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    switch (s) {
        case proto::user_io::PollShape::InfoPoll:
        case proto::user_io::PollShape::F12ModPoll:
            return 0x01;
        case proto::user_io::PollShape::RumblePoll:

            if (const auto m = rumble_pads_reader_.take_if_changed(input_.rumble_pads_cell()))
                rumble_pads_ = *m;
            return rumble_pads_;
        case proto::user_io::PollShape::kCount:
            break;
    }
    return 0;
}

std::size_t LinkSession::put_census_row_(std::span<reactor::LinkDecoderDecl> out, std::size_t n,
                                         const reactor::LinkDecoderDecl& row) noexcept {
    if (n < out.size()) {
        out[n] = row;
        return n + 1;
    }
    ++census_row_drops_;
    return n;
}

std::span<const reactor::LinkDecoderDecl> LinkSession::census_rows_(
    std::array<reactor::LinkDecoderDecl, kMaxServices>& out) noexcept {
    std::size_t n = 0;

    if (current_.has_value() && session_.type() == proto::CoreType::EightBit) {
        const cores::CoreProfile& p = current_->profile();
        if (!p.is_front_end && p.block_service == cores::BlockService::Generic) {
            n = put_census_row_(out, n,
                                reactor::LinkDecoderDecl{"link.block", reactor::Cause::Tick,
                                                         &block_row_, 0, reactor::DeadlineClass::A,
                                                         reactor::OsdBudget::Claimed});
        }
    }
    if (current_.has_value()) {
        for (const reactor::LinkDecoderDecl& r : current_->services())
            n = put_census_row_(out, n, r);
    }

    if (current_.has_value() && session_.type() == proto::CoreType::EightBit &&
        !cores::profile_for(conf_str_name()).is_front_end) {
        n = put_census_row_(out, n,
                            reactor::LinkDecoderDecl{"link.status_poll", reactor::Cause::Tick,
                                                     &status_row_, 0, reactor::DeadlineClass::A,
                                                     reactor::OsdBudget::Shared});

        if (bindings_.declares_info())
            n = put_census_row_(out, n, info_poll_.census_row(info_row_));

        n = put_census_row_(out, n, rumble_poll_.census_row(rumble_row_));
    }
    return std::span<const reactor::LinkDecoderDecl>(out.data(), n);
}

Ex<LinkSession::SaveBind> LinkSession::bind_save_(proto::SlotIndex si, std::string_view path,
                                                  bool manual) {
    TASTY_SEAT_BODY(LinkSession);
    if (save_src_ == nullptr || path.empty()) return SaveBind::Ready;
    using Attach = SaveImageSource::Attach;
    switch (save_src_->attach_state(si)) {
        case Attach::Attached:
            if (save_src_->path_of(si) == path && save_src_->manual(si) == manual &&
                !save_src_->close_owed(si))
                return SaveBind::Ready;
            begin_detach_(si);
            return SaveBind::Declined;
        case Attach::Attaching:
        case Attach::Detaching:

            return SaveBind::Declined;
        case Attach::Detached:
            break;
    }

    const cores::BlankSaveSpec blank =
        current_.has_value() ? current_->profile().blank_save : cores::BlankSaveSpec{};
    if (!save_src_->bind(si, path, manual, blank)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), si.v});
    }

    if (!storage_life_) {
        save_src_->note_attached(si, false, proto::FileSize{});
        return SaveBind::Ready;
    }
    svc::IStorageBackend* const backend = save_src_->backend(si);
    if (backend == nullptr) return std::unexpected(Error{Errc::slot_range, ERR_SITE(), si.v});

    if (!storage_life_->stage_backend(si, *backend, save_src_->order(si)))
        return SaveBind::Declined;
    if (!slots_.submit_attach(si)) {
        ++stage_refusals_;
        save_src_->note_attached(si, false, proto::FileSize{});
        return SaveBind::Ready;
    }
    save_src_->mark(si, Attach::Attaching);
    return SaveBind::Waiting;
}

std::uint64_t LinkSession::save_extent_of_(proto::SlotIndex slot) const noexcept {
    return save_src_ == nullptr ? 0 : save_src_->size_of(slot).v;
}

void LinkSession::begin_detach_(proto::SlotIndex si) noexcept {
    using Attach = SaveImageSource::Attach;
    if (save_src_ == nullptr) return;
    const Attach state = save_src_->attach_state(si);
    if (state != Attach::Attached && state != Attach::Attaching) return;

    if (!storage_life_) {
        save_src_->note_detached(si);
        return;
    }

    if (state == Attach::Attaching) {
        save_src_->owe_close(si);
        return;
    }

    if (!slots_.submit_detach(si)) {
        ++stage_refusals_;
        return;
    }
    save_src_->mark(si, Attach::Detaching);
}

void LinkSession::release_save_slots_() noexcept {
    for (unsigned i = 0; i < SaveImageSource::kSlots; ++i) {
        begin_detach_(proto::SlotIndex{static_cast<std::uint8_t>(i)});
    }
}

ILinkEncoder::Outcome LinkSession::perform_bound_mount_(
    const proto::LinkOp::BindMount& bm, const std::optional<SessionBindings::Mount>& image) {
    if (!bm.perform) return ILinkEncoder::Outcome::Encoded;
    if (!current_.has_value()) {
        push(Event::InfoRequest{.id = InfoId::ImageMountFailed});
        return ILinkEncoder::Outcome::Dropped;
    }
    RtRelaxScope scope(ops_);
    Ex<void> r{};
    if (bm.image == proto::FileId{}) {
        r = current_->mount(bm.index, cores::MountedPath{});
    } else if (image.has_value()) {
        r = current_->mount(bm.index, cores::MountedPath{proto::PathId{0}, image->path.view(),
                                                         proto::FileSize{image->size_bytes}});
    } else {
        r = std::unexpected(Error{Errc::not_found, ERR_SITE(), bm.index.v});
    }
    if (!r) {

        last_error_ = r.error();
        push(Event::InfoRequest{.id = InfoId::ImageMountFailed});
    }
    publish_option_table();
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome LinkSession::apply_save_bind_(
    const proto::LinkOp::BindSlot& bind, infra::OptRef<const ILinkTxReader> inbox) noexcept {
    const proto::SlotIndex si = bind.slot;
    const bool pendable = si.v < SaveImageSource::kSlots;

    OwedSaveBind& owed = owed_save_[pendable ? si.v : kOffSlot];
    owed.arm(bind.act_gen, save_body_(0));
    if (bind.bind != proto::LinkOp::SlotBind::Attach) {
        begin_detach_(si);

        const bool refused = save_src_ != nullptr &&
                             save_src_->attach_state(si) == SaveImageSource::Attach::Attached;
        owed.answer(refused ? cores::SaveVerdict::Declined : cores::SaveVerdict::Known,
                    save_body_(0));
        return ILinkEncoder::Outcome::Encoded;
    }
    const InternedFile* const file = inbox ? inbox->file(bind.path) : nullptr;
    if (file == nullptr) {
        owed.answer(cores::SaveVerdict::Declined, save_body_(0));
        return ILinkEncoder::Outcome::Dropped;
    }
    auto bound = bind_save_(si, file->path.view(), bind.manual);
    if (!bound) {
        owed.answer(cores::SaveVerdict::Declined, save_body_(0));
        return ILinkEncoder::Outcome::Dropped;
    }
    switch (*bound) {
        case SaveBind::Ready:
            owed.answer(cores::SaveVerdict::Known, save_body_(save_extent_of_(si)));
            break;
        case SaveBind::Waiting:
            break;
        case SaveBind::Declined:
            owed.answer(cores::SaveVerdict::Declined, save_body_(0));
            break;
    }
    return ILinkEncoder::Outcome::Encoded;
}

void LinkSession::settle_save_binds_() noexcept {
    if (save_src_ == nullptr) return;
    using Attach = SaveImageSource::Attach;
    for (unsigned i = 0; i < SaveImageSource::kSlots; ++i) {
        const proto::SlotIndex si{static_cast<std::uint8_t>(i)};

        if (save_src_->close_owed(si) && save_src_->attach_state(si) == Attach::Attached)
            begin_detach_(si);
        if (!owed_save_[i].owed()) continue;
        const Attach state = save_src_->attach_state(si);
        if (state == Attach::Attaching) continue;

        const bool known = state == Attach::Attached;
        owed_save_[i].answer(known ? cores::SaveVerdict::Known : cores::SaveVerdict::Declined,
                             save_body_(known ? save_extent_of_(si) : 0));
    }
}

void LinkSession::settle_stage_mount_() noexcept {
    if (!owed_mount_.owed()) return;

    if (!owed_mount_.parked()) {
        if (prefetch_ && !prefetch_->park_acked()) {
            if (clock().now().count() < owed_mount_.quiesce_deadline_ns()) {
                ++prefetch_park_deferrals_;
                return;
            }

            last_error_ = Error{Errc::timeout, ERR_SITE(), 0};
            ++prefetch_park_timeouts_;
        }
        owed_mount_.mark_parked();
    }
    cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;
    const cores::MountState ms =
        cd == nullptr ? cores::MountState::Failed : cd->mount_disc(owed_mount_.path());
    if (ms == cores::MountState::Pending) return;
    after_mount_answer_();
    owed_mount_.answer(ms == cores::MountState::Done ? cores::MountVerdict::Done
                                                     : cores::MountVerdict::Failed,
                       mount_body_());
}

bool LinkSession::drain_payload_(std::span<const std::uint8_t> bytes, std::uint16_t chunk_bytes,
                                 std::uint64_t at) noexcept {
    const auto chunk = static_cast<std::size_t>(chunk_bytes);
    if (chunk == 0) return stage_open_->write(bytes).has_value();
    std::size_t off = 0;
    while (off < bytes.size()) {
        const std::size_t n = std::min(chunk, bytes.size() - off);
        if (auto w = stage_open_->write(bytes.subspan(off, n)); !w) return false;
        off += n;
        stage_tick_.advance(at + off);
    }
    return true;
}

void LinkSession::after_mount_answer_() noexcept { publish_option_table(); }

cores::MountStatus LinkSession::mount_body_() noexcept {
    cores::MountStatus body{};
    if (cores::IStagingCore* cd = current_.has_value() ? current_->staging_core() : nullptr;
        cd != nullptr) {
        const cores::StagePlan plan = cd->stage_plan();
        body.disc_size_bytes = plan.disc_size_bytes;
        body.data_first_track = plan.data_first_track;
        body.game_id = plan.game_id;
    }
    return body;
}

cores::SaveExtent LinkSession::save_body_(std::uint64_t size_bytes) noexcept {
    cores::SaveExtent body{};
    body.size_bytes = size_bytes;
    return body;
}

Ex<void> LinkSession::apply_bind_slot_(proto::SlotIndex slot, proto::LinkOp::SlotBind kind,
                                       proto::FileSize size, proto::PathId path_id) {
    switch (kind) {
        case proto::LinkOp::SlotBind::Mount:
            return slots_.mount(link_, slot, size, path_id);
        case proto::LinkOp::SlotBind::Unmount:
            return slots_.unmount(link_, slot);
        case proto::LinkOp::SlotBind::MountCd:
            return slots_.mount_cd(link_, slot, size);
        case proto::LinkOp::SlotBind::Attach:
        case proto::LinkOp::SlotBind::Detach:
            break;
    }
    return std::unexpected(
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(infra::ordinal(kind))});
}

proto::SlotRoles LinkSession::current_slot_roles_() noexcept {
    proto::IResidentImageSource* const resident = current_->block_source();
    proto::IBlockGeometry* const geometry = current_->block_geometry();
    return proto::SlotRoles{
        .resident = resident != nullptr ? infra::OptRef<proto::IResidentImageSource>{*resident}
                                        : infra::OptRef<proto::IResidentImageSource>{},
        .geometry = geometry != nullptr ? infra::OptRef<proto::IBlockGeometry>{*geometry}
                                        : infra::OptRef<proto::IBlockGeometry>{}};
}

void LinkSession::apply_bind_slot_roles_(proto::SlotIndex slot,
                                         const proto::SlotRoles& roles) noexcept {
    slots_.bind_roles(slot, roles);
}

void LinkSession::mount_save_channel(cores::ISaveChannel& channel) {
    TASTY_SEAT_BODY(LinkSession);
    if (!vfs_) return;
    const auto count = channel.claim_mount_plan();
    if (!count) return;
    const std::uint16_t previous = channel_slots_;
    unmount_save_channel();

    release_channel_slots_(previous, *count);

    cores::ISaveChannel::Mount mounts[kMaxChannelSlots]{};
    bool planned[kMaxChannelSlots]{};
    const std::size_t n = *count < kMaxChannelSlots ? *count : kMaxChannelSlots;
    for (std::size_t i = 0; i < n; ++i) {
        auto m = channel.mount_at(i);
        if (!m) {
            last_error_ = m.error();
            continue;
        }
        mounts[i] = *m;
        planned[i] = true;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!planned[i]) continue;
        const proto::SlotIndex si{mounts[i].slot.v};

        if ((poisoned_slots_() & (1u << si.v)) != 0u) {
            ++stage_refusals_;
            last_error_ = Error{Errc::would_block, ERR_SITE(), si.v};
            continue;
        }
        if (!storage_life_) {
            channel.descriptor().note_attached(si, false, proto::FileSize{});
        } else {
            svc::IStorageBackend* const b = channel.backend(i);
            const bool staged =
                b != nullptr && storage_life_->stage_backend(si, *b, channel.order(i));
            if (staged)
                channel_borrows_ = static_cast<std::uint16_t>(channel_borrows_ | (1u << si.v));
            if (!staged || !slots_.submit_attach(si)) {
                ++stage_refusals_;
                channel.descriptor().note_attached(si, false, proto::FileSize{});
            }
        }
        apply_bind_slot_roles_(
            si, proto::SlotRoles{.descriptor =
                                     infra::OptRef<proto::IImageSource>{channel.descriptor()}});
        if (auto r = apply_bind_slot_(si, proto::LinkOp::SlotBind::Mount, mounts[i].size,
                                      proto::PathId{0});
            !r) {
            apply_bind_slot_roles_(si, proto::SlotRoles{});
            last_error_ = r.error();
            continue;
        }
        channel_slots_ = static_cast<std::uint16_t>(channel_slots_ | (1u << si.v));
        log_edge(LogRec::Mount{si.v});
    }
}

void LinkSession::unmount_save_channel() {
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        if ((channel_slots_ & (1u << i)) == 0u) continue;
        const proto::SlotIndex si{i};
        apply_bind_slot_roles_(si, proto::SlotRoles{});
        if (auto r = apply_bind_slot_(si, proto::LinkOp::SlotBind::Unmount, proto::FileSize{},
                                      proto::PathId{});
            r)
            log_edge(LogRec::Unmount{si.v});
    }
    channel_slots_ = 0;
}

void LinkSession::release_channel_slots_(std::uint16_t mask, std::size_t keep) noexcept {
    (void)slots_.install_completions();
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        const proto::SlotIndex si{i};
        if (i >= keep && storage_life_) {
            if (!slots_.submit_detach(si)) ++stage_refusals_;
            continue;
        }
        if (!storage_life_) continue;
        if (storage_life_->release_slot(si)) {
            channel_borrows_ = static_cast<std::uint16_t>(channel_borrows_ & ~(1u << i));
        } else {
            ++channel_release_refusals_;
        }
    }
}

Ex<void> LinkSession::perform_file_tx_(svc::IFile& f, std::string_view path, std::uint8_t slot,
                                       ContentEdge edge) {
    const std::size_t dot = path.rfind('.');
    current_->set_pending_file_ext(dot == std::string_view::npos ? std::string_view{}
                                                                 : path.substr(dot));
    current_->set_pending_file_path(path);

    ProgressImageSink& meter = binder_.file_progress();
    const auto sz = f.size();
    meter.arm(*this, sz ? sz->v : 0, this);
    const auto r = current_->file_tx(proto::IoIndex{slot}, f);
    meter.disarm();
    if (!r) return r;
    after_file_tx_(slot, path, edge);
    return {};
}

void LinkSession::after_file_tx_(std::uint8_t slot, std::string_view path, ContentEdge edge) {
    ++ftx_count_;
    ftx_index_ = slot;
    ftx_bytes_ = current_->last_tx_bytes();

    if (bindings_.declares_cheats() && edge != ContentEdge::Silent) {
        cheats_.content_loaded(core_ptr_(), TxDigest::Kind::Load,
                               edge == ContentEdge::ChainEnd ? std::string_view{} : path,
                               current_->last_tx_crc(), false);
    }
    push(Event::SdActivity{.slot = slot});
    if (cores::ISaveChannel* ch = current_->save_channel()) {
        mount_save_channel(*ch);
    }
}

void LinkSession::before_close() noexcept {
    TASTY_SEAT_BODY(LinkSession);
    announce_open_save_();
    if (!current_.has_value()) return;
    if (cores::ISaveChannel* ch = current_->save_channel()) mount_save_channel(*ch);
}

void LinkSession::on_load_progress(std::uint16_t cur, std::uint16_t max) noexcept {
    TASTY_SEAT_BODY(LinkSession);

    push(Event::ProgressUpdate{.cur = cur, .max = max});

    if (link_.posted()) return;

    const bool done = max == 0;

    if (!done && !progress_opened_osd_ && !osd_.lit()) {
        progress_opened_osd_ = true;
        (void)osd_.set_show(proto::LinkOp::OsdShow::Overlay);
    } else if (done && progress_opened_osd_) {
        progress_opened_osd_ = false;
        (void)osd_.set_show(proto::LinkOp::OsdShow::Off);
    }

    for (unsigned i = 0; i < proto::OsdSurface::kMaxRows && osd_.flush_one(); ++i) {
    }
}

Ex<void> LinkSession::detach() {

    const std::uint64_t t0 = log_now_ns();

    if (copy_ds_) {
        copy_ds_->abandon();
        end_copy_(false);
    }
    if (link_.posted()) {
        link_.abandon_post();
        ++ftx_hold_abandons_;
    }

    cut_file_tx_();
    cut_stage_();
    if (window_jobs_) window_jobs_->forget_held();

    binder_.fio_queue().abandon();

    if (auto r = flush_save_upload(); !r) {
        ++save_upload_errs_;
    } else if (*r != 0) {
        ++save_uploads_;
    }

    owed_mount_.strand();
    for (OwedSaveBind& owed : owed_save_)
        owed.strand();

    channel_slots_ = 0;
    slots_.reset_all();

    release_save_slots_();
    if (save_src_ != nullptr) save_src_->unbind_all();

    if (auto r = binder_.unbind_session(); !r) return r;

    if (prefetch_) {
        if (!prefetch_->park_acked()) {
            last_error_ = Error{Errc::would_block, ERR_SITE(), 0};
            ++prefetch_park_deferrals_;
        }
    }

    if (current_.has_value()) {

        if (auto r = current_->shutdown(); !r) {
            push(Event::SessionAdvisory{.why = r.error().code});
        }

        current_->retire();
        ++core_shutdowns_;

        if (retiring_.gen != 0) abandon_retiring_(false);
        retiring_ = Retiree{.slot = adopted_slot_,
                            .gen = adopted_gen_,
                            .seat = storage_life_,
                            .mask = storage_life_ ? channel_borrows_ : std::uint16_t{0},
                            .sent = 0};
        current_.reset();
        adopted_slot_ = proto::LinkOp::AdoptCore::kNoSlot;
        adopted_gen_ = 0;
        retire_due_ns_ = clock().now().count() + kCoreRetireBoundNs;
        retire_t0_ = t0;
        retire_logs_ = true;
    }
    channel_borrows_ = 0;
    drop_owed_ = true;
    (void)slots_.install_completions();
    settle_retire_(RetireEnd::Poll);

    session_ = proto::CoreSession(link_, fenced_signals_, &binder_.fio_queue());
    return {};
}

bool LinkSession::poll_retiree_(Retiree& r) noexcept {
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        const auto bit = static_cast<std::uint16_t>(1u << i);
        if ((r.mask & bit) == 0u) continue;
        const proto::SlotIndex si{i};
        if ((r.sent & bit) == 0u && slots_.submit_detach(si)) {
            r.sent = static_cast<std::uint16_t>(r.sent | bit);
            continue;
        }

        if (!r.seat || r.seat->release_slot(si)) {
            r.mask = static_cast<std::uint16_t>(r.mask & ~bit);
        }
    }
    return r.mask == 0u;
}

void LinkSession::settle_retire_(RetireEnd end) noexcept {
    const bool polling = end == RetireEnd::Poll || end == RetireEnd::Round;
    if (retiring_.gen != 0) {
        const bool home = poll_retiree_(retiring_);
        const bool expired = clock().now().count() >= retire_due_ns_;
        if (home) {
            release_retiree_(retiring_);
            retiring_ = Retiree{};
        } else if (polling && !expired) {
            settle_abandoned_();
            return;
        } else {
            abandon_retiring_(end != RetireEnd::Exit && expired);
        }
    }
    settle_abandoned_();
    if (!drop_owed_) return;
    drop_owed_ = false;

    if (discs_) discs_->release();

    binder_.release_feeder();
    if (retire_logs_) {
        retire_logs_ = false;
        log_edge(LogRec::CoreUnload{sat_u32((log_now_ns() - retire_t0_) / 1'000'000u)});
    }
}

void LinkSession::abandon_retiring_(bool counted) noexcept {
    if (counted) {
        ++core_abandons_;
        last_error_ = Error{Errc::timeout, ERR_SITE(), retiring_.mask};
    }
    for (Retiree& a : abandoned_) {
        if (a.gen != 0) continue;
        a = retiring_;
        retiring_ = Retiree{};
        return;
    }

    Retiree& into = abandoned_.front();
    into.merge(retiring_);
    retiring_ = Retiree{};
}

void LinkSession::release_retiree_(const Retiree& r) noexcept {
    const auto gens = r.bay_gens();
    for (std::size_t i = 0; i < kCoreBaySlots; ++i)
        if (gens[i] != 0) release_(static_cast<std::uint8_t>(i), gens[i]);
}

void LinkSession::settle_abandoned_() noexcept {
    for (Retiree& a : abandoned_) {
        if (a.gen == 0) continue;
        if (a.mask != 0u && !poll_retiree_(a)) continue;
        release_retiree_(a);
        a = Retiree{};
    }
}

std::uint16_t LinkSession::poisoned_slots_() const noexcept {
    std::uint16_t m = 0;
    for (const Retiree& a : abandoned_)
        m = static_cast<std::uint16_t>(m | a.mask);
    return m;
}

void LinkSession::reap_storage() noexcept {
    (void)slots_.install_completions();
    settle_retire_(RetireEnd::Round);
    settle_save_binds_();
    settle_stage_mount_();
}

void LinkSession::gather_diag(DiagCounters& out) const noexcept {
    out.link = static_cast<std::uint8_t>((session_live() ? 1u : 0u) | (park_.parked() ? 2u : 0u) |
                                         (start_.on() ? 4u : 0u));
    out.core_shutdowns = core_shutdowns_;
    out.stage_status_change = status_decoder_.last();
    out.status_reply_refusals = status_decoder_.flush_refusals();

    out.prefetch_park_timeouts = prefetch_park_timeouts_;
    out.stale_owner_steps = stale_owner_steps_;
    out.start_answer_drops = start_answer_drops();
    out.core_abandons = core_abandons_;
    out.info_poll_pushes = info_poll_.pushes();
    out.fed_refusals = binder_.fed_refusals();

    out.disc_sync_decompress = disc_sync_decompress_.load(std::memory_order_relaxed);
    out.disc_park_timeouts = disc_park_timeouts_.load(std::memory_order_relaxed);
    out.disc_prefetch_refusals = disc_prefetch_refusals_.load(std::memory_order_relaxed);
    out.cd_flow_waits = cd_flow_waits_;
    out.cd_cdda_sectors = cd_cdda_sectors_;
    out.cd_data_sectors = cd_data_sectors_;
    out.cd_idle_ticks = cd_idle_ticks_;
    out.cd_gated_ticks = cd_gated_ticks_;
    out.cd_drive_state = cd_drive_state_;
    out.cd_drive_track = cd_drive_track_;
    out.cd_drive_lba = cd_drive_lba_;
    out.cd_drive_audio_lba = cd_drive_audio_lba_;
    out.cd_drive_is_data = cd_drive_is_data_;
    out.cd_substitutes = cd_substitutes_;
    out.cd_subcode_substitutes = cd_subcode_substitutes_;
    out.cd_not_resident = cd_not_resident_;
    out.cd_busy_ticks = cd_busy_ticks_;
    out.cd_egress_abandons = cd_egress_abandons_;
    out.last_error_code = static_cast<std::uint16_t>(last_error_.code);
    out.last_error_site = last_error_.site;

    out.last_error_detail = last_error_.detail;

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
    out.file_tx_count = ftx_count_;
    out.file_tx_bytes = ftx_bytes_;
    out.file_tx_index = ftx_index_;

    const proto::SpiBlockDecoder::Counters& bc = block_decoder_.counters();
    out.blk_rounds = bc.passes;
    out.blk_served = bc.served;
    out.blk_errors = bc.errors;
    out.blk_err_code = bc.err_code;
    const proto::BlockSlots::Diagnostics& bd = slots_.diagnostics();
    const proto::DecodeCounters& dc = slots_.block_poll().diagnostics();
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

void LinkSession::diag_counters(DiagCounters& out) const noexcept { out = diag_.sample().value; }

namespace {

template <std::size_t N>
void copy_cell_text(char (&dst)[N], std::string_view src) noexcept {
    const std::size_t n = src.size() < N - 1 ? src.size() : N - 1;
    if (n != 0) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}
}  // namespace

void LinkSession::publish_config_slots() noexcept {
    cores::IConfigSlots* cs = current_.has_value() ? current_->config_slots() : nullptr;
    if (cs == nullptr || !vfs_) config_slot_scratch_ = ConfigSlotTable{};
    config_slot_cell_.publish(config_slot_scratch_);
}

void LinkSession::publish_option_table() noexcept {
    cores::IOptionRows* rows = current_.has_value() ? current_->options() : nullptr;
    option_scratch_ = OptionTable{};
    if (rows != nullptr) {
        const cores::CoreProfile& p = current_->profile();
        const auto decls = rows->option_rows();
        const std::size_t cap = std::size(option_scratch_.rows);
        const std::size_t n = decls.size() < cap ? decls.size() : cap;
        option_scratch_.count = static_cast<std::uint8_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const cores::OptionRowView v = rows->option_view(i);
            OptionTable::Row& row = option_scratch_.rows[i];
            row.decl = infra::opt_ref_of(&decls[i]);
            row.current = v.current;
            row.dimmed = v.dimmed;
            (void)row.text.assign(v.text);

            if (decls[i].kind == cores::OptionRow::Kind::File) {
                for (const cores::FileSlot& fs : p.slots) {
                    if (fs.index == decls[i].slot) {
                        (void)row.ext.assign(fs.extensions);
                        break;
                    }
                }
            }
        }
        const std::size_t pages = std::size(option_scratch_.page_title);
        for (std::size_t i = 0; i < p.option_pages.size() && i < pages; ++i) {
            (void)option_scratch_.page_title[i].assign(p.option_pages[i]);
        }
    }
    option_cell_.publish(option_scratch_);
}

void LinkSession::publish_dip_table() noexcept {
    dip_scratch_ = DipTable{};
    cores::IDipSwitches* sw = current_.has_value() ? current_->dip_switches() : nullptr;
    if (sw != nullptr) {
        const std::size_t cap = std::size(dip_scratch_.rows);
        const std::size_t n = sw->dip_count() < cap ? sw->dip_count() : cap;
        dip_scratch_.count = static_cast<std::uint8_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const cores::DipRowView rv = sw->dip_row(i);
            DipTable::Row& row = dip_scratch_.rows[i];
            copy_cell_text(row.name, rv.name);
            const std::size_t ids = std::size(row.ids);
            row.choices = static_cast<std::uint8_t>(rv.choices < ids ? rv.choices : ids);
            row.current = rv.current < row.choices ? rv.current : 0;
            for (std::size_t m = 0; m < row.choices; ++m) {
                copy_cell_text(row.ids[m], sw->dip_choice(i, m));
            }
        }
    }
    dip_cell_.publish(dip_scratch_);
}

void LinkSession::publish_status_word() noexcept {
    status_scratch_ = session_.status().value();
    status_cell_.publish(status_scratch_);
}

void LinkSession::publish_doorbell_stats() noexcept {
    const DoorbellStats d = binder_.doorbell_stats();

    if (d.declared == doorbell_last_.declared && d.bound == doorbell_last_.bound &&
        d.refusals == doorbell_last_.refusals && d.fallbacks == doorbell_last_.fallbacks &&
        d.retirements == doorbell_last_.retirements &&
        d.release_failures == doorbell_last_.release_failures) {
        return;
    }
    doorbell_last_ = d;
    doorbell_cell_.publish(d);
}

bool LinkSession::osd_focused() const noexcept {
    TASTY_SEAT_BODY(LinkSession);
    return osd_.has_focus();
}

void LinkSession::poll_osd_mask() {

    if (rebooting_) return;

    if (start_.on() || park_.engaged()) return;
    const bool vis = osd_.has_focus();
    if (vis != osd_visible_last_) {
        osd_visible_last_ = vis;
        on_osd_visibility_edge(vis);
    }
    osd_mask_decoder_.service();
}

void LinkSession::on_osd_visibility_edge(bool visible) {
    if (auto r = flush_save_upload(); !r) {
        ++save_upload_errs_;
        log_edge(LogRec::Refusal{r.error().code, r.error().site});
    } else if (*r != 0) {
        ++save_uploads_;
    }
    if (visible && current_.has_value() && window_jobs_) {
        if (cores::IWindowSave* role = current_->window_save(); role != nullptr) {
            (void)window_jobs_->arm_osd_open(*role);
        }
    }
    if (visible || !current_.has_value()) return;

    ++osd_close_hooks_;
    if (auto r = current_->osd_closed(); !r) {
        log_edge(LogRec::Refusal{r.error().code, r.error().site});
    }
}

Ex<std::size_t> LinkSession::write_save_upload_(cores::ISaveUpload& up) {
    auto img = up.pull_save();
    if (!img) return std::unexpected(img.error());
    const auto rel = config_rel("config/nvram/", up.save_file_name());
    if (rel.empty()) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});

    if (auto r = arm_or_write_(rel.view(), std::as_bytes(*img)); !r) {
        return std::unexpected(r.error());
    }
    return img->size();
}

Ex<std::size_t> LinkSession::flush_save_upload() {
    if (!current_.has_value()) return std::size_t{0};
    if (save_pull_fenced_()) return std::size_t{0};

    cores::ISaveUpload* up = current_->save_upload();
    if (up == nullptr) return std::size_t{0};
    if (!vfs_) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    if (up->save_file_name().empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    auto req = proto::read_save_request(link_);
    if (!req) return std::unexpected(req.error());
    if (!req->requested()) return std::size_t{0};
    return write_save_upload_(*up);
}

bool LinkSession::save_upload_counted() noexcept {
    TASTY_SEAT_BODY(LinkSession);
    auto s = save_upload_now();
    if (!s) {
        ++save_upload_errs_;
        log_edge(LogRec::Refusal{s.error().code, s.error().site});
        return false;
    }
    if (*s != 0) ++save_uploads_;
    return true;
}

bool LinkSession::set_dip_counted(std::uint8_t row, std::uint32_t choice) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    cores::IDipSwitches* sw = current_.has_value() ? current_->dip_switches() : nullptr;
    if (sw == nullptr) return false;
    if (auto r = sw->set_dip(row, choice); !r) return false;
    ++dip_sets_;
    publish_dip_table();
    return true;
}

bool LinkSession::set_option_counted(std::uint8_t row, std::uint8_t choice) noexcept {
    TASTY_SEAT_BODY(LinkSession);
    cores::IOptionRows* rows = current_.has_value() ? current_->options() : nullptr;
    if (rows == nullptr) return false;
    if (auto r = rows->set_option(row, choice); !r) return false;
    ++option_sets_;
    publish_option_table();
    return true;
}

bool LinkSession::settle_options_counted() noexcept {
    TASTY_SEAT_BODY(LinkSession);
    cores::IOptionRows* rows = current_.has_value() ? current_->options() : nullptr;
    if (rows == nullptr) return false;
    if (auto r = rows->apply_armed_reset(); !r) return false;
    ++option_sets_;
    publish_option_table();
    return true;
}

Ex<std::size_t> LinkSession::save_upload_now() {
    if (!current_.has_value()) return std::size_t{0};
    if (save_pull_fenced_()) return std::size_t{0};
    cores::ISaveUpload* up = current_->save_upload();
    if (up == nullptr) return std::size_t{0};
    if (!vfs_) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (up->save_file_name().empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return write_save_upload_(*up);
}

Ex<void> LinkSession::adopt_(const proto::LinkOp::AdoptCore& a) {
    using Built = proto::LinkOp::Built;

    if (drop_owed_) settle_retire_(RetireEnd::Adopt);
    if (mailbox_) mailbox_->forget();
    switch (a.outcome) {
        case Built::Core:
            break;
        case Built::NoName:
            return {};
        case Built::NoStorage:

            machine_.close(Event::ConfStrOnlySession{.cause = Event::ConfStrOnlyCause::NoStorage},
                           EmitSite{ERR_SITE()});
            return {};
        case Built::RegistryMiss:
            machine_.close(
                Event::ConfStrOnlySession{.cause = Event::ConfStrOnlyCause::RegistryMiss},
                EmitSite{ERR_SITE()});
            return {};
        case Built::Refused:
            return std::unexpected(Error{a.err, ERR_SITE(), 0});
    }

    if (a.slot >= kCoreBaySlots || bay_.at(a.slot).gen != a.gen || !bay_.at(a.slot).core) {
        ++stale_adopts_;
        release_(a.slot, a.gen);
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), a.slot});
    }

    if (current_) {

        if (a.slot != adopted_slot_) release_(a.slot, a.gen);
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
    }
    BuiltCore& built = bay_.at(a.slot);
    current_ = infra::OptRef<cores::Core>{*built.core};
    adopted_slot_ = a.slot;
    adopted_gen_ = a.gen;

    if (built.fed_ring.has_value()) (void)binder_.adopt_fed_ring(std::move(*built.fed_ring));
    const cores::CoreAdopt adopting{.session = session_,
                                    .declares_turbo = bindings_.declares_turbo()};
    if (auto r = current_->adopt(adopting); !r) {
        binder_.release_feeder();
        drop_borrow_();
        return r;
    }
    return {};
}

void LinkSession::drop_borrow_() noexcept {
    current_.reset();
    release_(adopted_slot_, adopted_gen_);
    adopted_slot_ = proto::LinkOp::AdoptCore::kNoSlot;
    adopted_gen_ = 0;
}

void LinkSession::release_(std::uint8_t slot, std::uint16_t gen) noexcept {
    if (slot >= released_.released_gen.size() || gen == 0) return;
    released_.released_gen[slot] = gen;
    releases_.publish(released_);
}

Ex<void> LinkSession::raise_session_() {
    const std::string name{conf_str_name()};

    const std::string_view eff = effective_name(name, bindings_.facts());

    if (session_.type() == proto::CoreType::EightBit) {

        if (auto r = session_.load_saved_config(); !r) return r;

        if (auto r = session_.send_rtc(); !r) return r;
    }

    if (current_.has_value()) {
        apply_bind_slot_roles_(proto::SlotIndex{current_->profile().staging.disc_slot.v},
                               current_slot_roles_());
    }

    if (current_.has_value()) {
        block_decoder_.bind_budget(current_->profile().block_drain_budget);
        block_decoder_.bind_sector_floor(current_->profile().block_sector_floor);
        std::array<reactor::LinkDecoderDecl, kMaxServices> rows{};
        const SessionBindings::Doorbells& db = bindings_.doorbells();
        const BuiltCore& built = bay_.at(adopted_slot_);
        if (auto r = binder_.bind_session(census_rows_(rows), core_ptr_(), db.table(), db.declared,
                                          built.lines);
            !r) {
            return r;
        }
    } else if (auto r = rebind_census({}); !r) {
        return r;
    }

    if (session_.type() == proto::CoreType::EightBit) {
        if (auto r = session_.release_reset(); !r) return r;
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

        (void)uart_.load_for_core(link_, eff, uart_capable, uart_token, midi_token, &files);
    }

    f12_mod_poll_.arm(true, bindings_.generation());
    f12_mod_poll_.start_beat();
    return {};
}

ExitReboot LinkSession::handoff_to_alternate_executable(const char* exe,
                                                        std::span<const char* const> argv) {

    ::sync();
    fenced_signals_.set_core_reset(true);
    const char* path = (argv.size() > 1 && argv[1] != nullptr) ? argv[1] : "";
    const char* xml = (argv.size() > 2) ? argv[2] : nullptr;
    if (xml != nullptr && xml[0] == '\0') xml = nullptr;

    const ::pid_t child = ::fork();
    if (child > 0) ::_exit(0);
    if (child == 0) {
        (void)::setsid();
        const ::pid_t grandchild = ::fork();
        if (grandchild > 0) ::_exit(0);
        if (grandchild == 0) {

            (void)::execl(exe, exe, path, xml, static_cast<char*>(nullptr));
        }
    }

    return ExitReboot::Cold;
}

Ex<void> LinkSession::re_exec(const LoadRequest& req) {

    ::sync();
    fenced_signals_.set_core_reset(true);

    const std::string app = self_exe_path();
    if (app.empty()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    const std::string path(req.path.view());
    const std::string xml(req.xml.view());

    const char* xml_arg = xml.empty() ? nullptr : xml.c_str();

    const ::pid_t child = ::fork();
    if (child > 0) ::_exit(0);
    if (child == 0) {
        (void)::setsid();
        const ::pid_t grandchild = ::fork();
        if (grandchild > 0) ::_exit(0);
        if (grandchild == 0) {
            (void)::execl(app.c_str(), app.c_str(), path.c_str(), xml_arg,
                          static_cast<char*>(nullptr));
        }
    }

    return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
}

}  // namespace mister::app
