// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "app/autosave_cells.h"
#include "app/input/rumble_players_cell.h"
#include "app/types.h"
#include "app/delivered.h"
#include "app/link_encoder.h"
#include "app/core_terms.h"
#include "infra/xthread/wake_flag.h"
#include "hal/pin_levels.h"
#include "proto/link_event.h"
#include "proto/link_op.h"
#include "proto/link_port.h"
#include "proto/link/download_session.h"
#include "app/board_ops.h"
#include "app/boot_args.h"
#include "app/boot_arm.h"
#include "app/reboot_answer.h"
#include "app/log_clock.h"
#include "app/core_tenancy.h"
#include "app/slot_bindings.h"
#include "app/core_option_cells.h"
#include "app/session_rows.h"
#include "app/held_transfer.h"
#include "app/ladder_cell.h"
#include "app/session_tally.h"
#include "app/diag_counters.h"
#include "app/event.h"
#include "app/fabric_state.h"
#include "app/quiesce_ack.h"
#include "app/identity_latch.h"
#include "app/identity_record.h"
#include "app/load_request.h"
#include "app/fenced_core_signals.h"
#include "app/link_binder.h"
#include "app/link_router.h"
#include "app/session_bindings.h"
#include "app/link_tx_channel.h"
#include "app/session_machine.h"
#include "app/start_in_flight.h"
#include "app/spi_encoder.h"
#include "proto/save_kind.h"
#include "app/nvram_upload.h"
#include "proto/osd_focus.h"
#include "proto/session_live.h"
#include "proto/link/spi_osd_mask_decoder.h"
#include "proto/link/spi_sampler.h"
#include "proto/link/poll_reader.h"
#include "app/save_image_source.h"
#include "app/ui_request.h"
#include "app/uart_mode.h"
#include "cores/cd_flow.h"
#include "cores/save_channel.h"
#include "cores/core_init_host.h"
#include "cores/save_copy_level.h"
#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/error.h"
#include "app/log_lane.h"
#include "infra/xthread/telemetry.h"
#include "infra/seat.h"
#include "infra/opt_ref.h"
#include "os/clock.h"

#include "reactor/executive.h"
#include "infra/prelude.h"
#include "reactor/link_decoder.h"
#include "hal/spi_transport.h"
#include "proto/link/core_session.h"
#include "proto/types.h"
#include "svc/input/joy_plan.h"

namespace mister::svc {
class Vfs;
}

namespace mister::svc::disc {
class DiscReadService;
}

namespace mister::svc::input {
class InputEmitter;
}

namespace mister::cores {
class Core;
class ISaveUpload;
}

namespace mister::app {

class VideoWire;
class ICheatApply;
class OsdWire;
class DiskRelays;
class SaveUploadRelay;
class WindowJobService;
template <SeatTag Consumer>
class LinkRxChannel;

namespace pcm {
class MailboxRelay;
}

struct SupervisorParts;

class LinkSession : private cores::ICoreInitHost,
                    private RebootAnswer::Host,
                    private CoreOptionCells::Host,
                    private proto::IOsdFocus,
                    private proto::ISessionLive {
    TASTY_SEAT_RESIDENT(RT);

public:
    LinkSession(
        hal::ISpiTransport& link,
        hal::ICoreSignals& signals,
        hal::ISpiSampleSource& samples,
        hal::PhysRegion aperture,
        EventQueue<SeatTag::RT>& events,
        const SupervisorParts& parts
    ) noexcept;
    ~LinkSession();

    LinkSession(const LinkSession&) = delete;
    LinkSession& operator=(const LinkSession&) = delete;

    using Port = proto::LinkPort<
        proto::LinkEvent::SaveBytes,
        proto::LinkEvent::CoreMade,
        proto::LinkEvent::StartRefused,
        proto::LinkEvent::RebootQuiesced>;

    [[nodiscard]] SessionState state() const noexcept { return machine_.state(); }

    proto::link::CoreSession& session() noexcept { return session_; }

    [[nodiscard]] const CoreTenancy& tenancy() const noexcept { return tenancy_; }

    [[nodiscard]] const HeldTransfer& held() const noexcept {
        TASTY_SEAT_BODY(LinkSession);
        return held_;
    }

    [[nodiscard]] HeldTransfer& held() noexcept {
        TASTY_SEAT_BODY(LinkSession);
        return held_;
    }

    [[nodiscard]] const SlotBindings& slot_bindings() const noexcept { return slot_bindings_; }

    [[nodiscard]] const CoreOptionCells& option_cells() const noexcept { return options_; }

    [[nodiscard]] const SessionRows& rows() const noexcept { return rows_; }

    [[nodiscard]] const proto::link::PollReader& f12_mod_poll() const noexcept {
        return f12_mod_poll_;
    }

    [[nodiscard]] const hal::PinLevelCell& pin_level_cell() const noexcept {
        return sampler_.levels();
    }

    const proto::link::SpiOsdMaskDecoder& osd_mask_decoder() const noexcept {
        return osd_mask_decoder_;
    }

    [[nodiscard]] svc::disc::DiscReadService& discs() noexcept { return discs_; }

    [[nodiscard]] const CoreTerms& core_terms() const noexcept { return terms_; }

    [[nodiscard]] CoreBay& core_bay() noexcept { return tenancy_.bay(); }

    [[nodiscard]] const BayReleasesCell& bay_releases() const noexcept {
        return tenancy_.releases();
    }

    [[nodiscard]] LinkTxChannel<SeatTag::Main>& link_inbox() noexcept { return link_inbox_; }

    [[nodiscard]] LinkTxChannel<SeatTag::Ui>& ui_inbox() noexcept { return ui_inbox_; }

    [[nodiscard]] LinkTxChannel<SeatTag::Input>& input_inbox() noexcept { return input_inbox_; }

    [[nodiscard]] LinkRxChannel<SeatTag::Main>& link_rx() noexcept { return link_rx_; }

    [[nodiscard]] IdentityRecordCell& identity_record_cell() noexcept {
        return identity_record_cell_;
    }

    [[nodiscard]] std::optional<proto::FioIndexByte> last_download() const noexcept {
        return held_.last_download();
    }

    [[nodiscard]] proto::LateAnswers take_late_answers() noexcept {
        return slot_bindings_.slots().take_late_answers();
    }

    enum class WireHold : std::uint8_t { None, Session, Transfer };

    [[nodiscard]] WireHold wire_hold() const noexcept {
        if (session_.configure_posted())
            return WireHold::Session;
        return held_.wire_held() ? WireHold::Transfer : WireHold::None;
    }

    [[nodiscard]] bool wire_held() const noexcept { return wire_hold() != WireHold::None; }

    [[nodiscard]] bool prelude_settled() noexcept;

    [[nodiscard]] const FileTxLevelCell& file_tx_level_cell() const noexcept {
        return held_.file_tx_level_cell();
    }

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::BindDecoders& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindFacts& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindConfig& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindUart& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::RebootNow& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::ApplyCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AbortSwitch& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindIdentity& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::DropCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AdoptCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SessionUp& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::CoreReset& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::LoadFacts& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::MailboxWrite& a, const LinkOpCtx& ctx) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    [[nodiscard]] bool declares_turbo() const noexcept;

    [[nodiscard]] const MraFacts& mra_facts() const noexcept { return bindings_.facts(); }

    [[nodiscard]] const SessionBindings& bindings() const noexcept { return bindings_; }

    [[nodiscard]] bool rebooting() const noexcept { return reboot_.rebooting(); }

    [[nodiscard]] const SessionTally& tally() const noexcept { return tally_; }

    [[nodiscard]] std::uint32_t start_answer_drops() const noexcept {
        return tally_.start_refused_drops
            + rows_.conf_str_decoder().lost()
            + tally_.core_made_drops;
    }

    void seal_boot_pump() noexcept;

    [[nodiscard]] DiskRelays& disks() noexcept { return disks_; }

    [[nodiscard]] SaveUploadRelay& saves() noexcept { return saves_; }

    [[nodiscard]] pcm::MailboxRelay& mailbox() noexcept { return mailbox_; }

    void settle_boot_slot_mount() noexcept;

    [[nodiscard]] NvramUpload::Tally nvram_tally() const noexcept { return nvram_.tally(); }

    using Identity = SessionIdentity;

    [[nodiscard]] std::optional<Identity> copy_identity() const noexcept {
        return identity_.copy();
    }

    [[nodiscard]] const IdentityLatch& identity() const noexcept { return identity_; }

    [[nodiscard]] DiagCounters diag_counters() const noexcept;

    [[nodiscard]] const DiagCountersCell& diag_cell() const noexcept { return diag_; }

    [[nodiscard]] const proto::StatusCell& status_cell() const noexcept {
        return rows_.status_cell();
    }

    [[nodiscard]] const DipLabelsCell& dip_labels_cell() const noexcept {
        return options_.dip_labels_cell();
    }

    [[nodiscard]] const DipChoicesCell& dip_choices_cell() const noexcept {
        return options_.dip_choices_cell();
    }

    [[nodiscard]] const OptionCell& option_cell() const noexcept { return options_.option_cell(); }

    [[nodiscard]] const ConfigValueCell& config_value_cell() const noexcept {
        return options_.config_value_cell();
    }

    [[nodiscard]] const MountStatusCell& mount_status_cell() const noexcept {
        return slot_bindings_.mount_status_cell();
    }

    [[nodiscard]] const SaveExtentCell& save_extent_cell() const noexcept {
        return slot_bindings_.save_extent_cell();
    }

    proto::link::BlockSlots& block_slots() noexcept { return slot_bindings_.slots(); }

    const SaveImageSource& save_source() const noexcept { return slot_bindings_.save_source(); }

    [[nodiscard]] LinkBinder& binder() noexcept { return binder_; }

    struct RoundEntry {
        bool live = false;
    };

    [[nodiscard]] RoundEntry poll(bool tick);

    void tick(RoundEntry entry);

    Delivered deliver(
        const proto::LinkOp& op,
        const ILinkTxReader& tx,
        bool windows_closed
    ) noexcept;

    void reap_storage() noexcept;

    void service_saves(proto::FioWordCount budget_words) noexcept;

    void poll_osd_mask();

    [[nodiscard]] Ex<std::size_t> flush_save_upload() {
        return nvram_.flush_save_upload(save_ctx_());
    }

    [[nodiscard]] Ex<std::size_t> save_upload_now() { return nvram_.save_upload_now(save_ctx_()); }

    [[nodiscard]] CoreEdgeSeq core_edge_seq() const noexcept { return core_edge_seq_; }

    [[nodiscard]] infra::OptRef<cores::Core> live_core() noexcept { return tenancy_.core(); }

    [[nodiscard]] Ex<std::optional<UiRequest::LoadCore>> boot(const BootArgs& args);

    void hold_core();

    [[nodiscard]] bool save_lane_fio_free() noexcept;
    [[nodiscard]] bool settle_save_step(bool fio_free);

    [[nodiscard]] const BoardOps& board_ops() const noexcept { return ops_; }

    [[nodiscard]] const os::IClock& clock() const noexcept { return clock_; }

    [[nodiscard]] bool session_starting() const noexcept { return start_.on(); }

    [[nodiscard]] const proto::ISessionLive& liveness() const noexcept { return *this; }

    [[nodiscard]] bool drops_live_pad(const proto::LinkOp& op) noexcept;
    [[nodiscard]] Ex<void> load_core(const LoadRequest& req);

    [[nodiscard]] Ex<ExitReboot> shutdown();

    void release_bus() noexcept;

    bool fpga_unconfigured() const noexcept { return fabric_.unconfigured(); }

    [[nodiscard]] const FabricStateView& fabric() const noexcept { return fabric_; }

    [[nodiscard]] const QuiesceAck& park() const noexcept { return park_; }

    bool ever_running() const noexcept { return ever_running_; }

private:
    [[nodiscard]] bool session_live() const noexcept override;

    [[nodiscard]] bool session_live_() const noexcept {
        return machine_.state() == SessionState::Running && !start_.on() && !park_.engaged();
    }

    [[nodiscard]] bool osd_focused() const noexcept override;

    [[nodiscard]] Ex<void> detach();

    void bind_routes_(
        infra::OptRef<const SessionRoutes> routes,
        proto::BuildGeneration gen,
        bool made
    ) noexcept;

    void drop_routes_() noexcept;

    void release_pads_() noexcept;

    [[nodiscard]] Ex<void> raise_session_(const proto::RtcImage& rtc);

    void arm_request();

    void apply_core_(const proto::LinkOp::ApplyCore& a);

    [[nodiscard]] bool names_park_(proto::BindGeneration g) const noexcept;

    void adopt_programmed_core_();

    [[nodiscard]] Ex<void> perform_session_up_(const proto::LinkOp::SessionUp& op);

    void refuse_start_(const Error& e) noexcept;

    [[nodiscard]] bool options_push_save_bytes(
        const CoreOptionCells::SaveAnswer& a
    ) noexcept override;
    [[nodiscard]] Ex<proto::RxSlabId> options_intern_save_bytes(
        std::span<const std::uint8_t> bytes
    ) noexcept override;
    [[nodiscard]] bool reboot_push_quiesced(proto::RebootSeq seq) noexcept override;

    [[nodiscard]] NvramUpload::SaveCtx save_ctx_() noexcept;

    void fail_machine_() noexcept;

    [[nodiscard]] bool fio_free_() noexcept;

    void push(const Event::SessionAdvisory& a);

    [[nodiscard]] std::string_view conf_str_name() const noexcept;

    bool core_supports_mgl() const noexcept;

    template <class A>
    void log_edge(const A& alt) noexcept;

    hal::ISpiTransport& link_;

    LinkBinder binder_;

    FencedCoreSignals fenced_signals_;

    SessionMachine machine_;

    infra::OptRef<const svc::Vfs> vfs_{};
    svc::disc::DiscReadService& discs_;
    svc::disc::DiscReadService& unbound_discs_;
    pcm::MailboxRelay& mailbox_;
    DiskRelays& disks_;
    SaveUploadRelay& saves_;
    NvramUpload nvram_;
    WindowJobService& window_jobs_;
    LinkTxChannel<SeatTag::Main>& link_inbox_;
    LinkTxChannel<SeatTag::Ui>& ui_inbox_;
    LinkTxChannel<SeatTag::Input>& input_inbox_;
    LinkRxChannel<SeatTag::Main>& link_rx_;
    LinkRouter& router_;
    Port out_;
    proto::link::SpiSampler sampler_;

    IdentityRecordCell& identity_record_cell_;

    BootArm boot_;

    StartInFlight start_;
    SessionTally tally_{};

    std::uint32_t disc_sync_decompress_ = 0;
    std::uint32_t disc_park_timeouts_ = 0;
    std::uint32_t disc_prefetch_refusals_ = 0;

    cores::CdFlow cd_flow_{};

    VideoWire& video_;
    const input::RumblePlayersCell& rumble_players_;
    OsdWire& osd_;

    proto::link::SpiOsdMaskDecoder osd_mask_decoder_;

    infra::OptRef<OsdMaskCell> osd_mask_cell_;

    IdentityLatch identity_;

    void publish_identity();

    void reset_video_geometry() override;
    void bind_video_prelude() override;
    void publish_core_identity() override;

    const BoardOps ops_;
    RebootAnswer reboot_;

    FabricCell& fabric_cell_;
    FabricStateView fabric_{};
    SwitchPark& switch_park_;
    QuiesceAck park_{};

    xthread::PauseGen dropped_gen_{};
    std::uint32_t holds_at_drop_ = 0;
    proto::BindGeneration torn_gen_{};
    proto::BindGeneration torn_done_{};

    void take_park_ask_() noexcept;

    void publish_fabric_();

    bool ever_running_ = false;

    proto::link::CoreSession session_;
    UartModeController uart_;

    SessionBindings bindings_{};

    bool core_ready_ = true;

    LogLane::Producer log_lane_;
    LogInstant load_start_{};

    DiagCountersCell diag_{};

    infra::OptRef<const xthread::Telemetry<std::uint32_t, SeatTag::Diag>> diag_ask_{};
    xthread::Telemetry<std::uint32_t, SeatTag::Diag>::Reader diag_ask_seen_{};

    void gather_diag(DiagCounters& out) const noexcept;

    void publish_diag() noexcept;

    void publish_diag_final() noexcept;

    void publish_diag_record() noexcept;
    void on_osd_visibility_edge(bool visible);

    ICheatApply& cheats_;

    infra::OptRef<const cores::SaveCopyCell> save_copy_;

    CoreOptionCells options_;

    const os::IClock& clock_;

    const CoreTerms terms_;

    CoreEdgeSeq core_edge_seq_{};

    SlotBindings slot_bindings_;

    SessionRows rows_;

    proto::link::PollReader f12_mod_poll_;

    CoreTenancy tenancy_;

    SpiEncoder encoder_;

    HeldTransfer held_;
};

}
