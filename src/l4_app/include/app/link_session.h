// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "app/file_stream_service.h"
#include "app/core_terms.h"
#include "infra/wake_flag.h"
#include "hal/pin_levels.h"
#include "proto/link_event.h"
#include "proto/link_op.h"
#include "proto/link_port.h"
#include "proto/download_session.h"
#include "app/board_ops.h"
#include "app/boot_arm.h"
#include "app/reboot_answer.h"
#include "app/config_pair.h"
#include "app/core_tenancy.h"
#include "app/slot_bindings.h"
#include "app/core_option_cells.h"
#include "app/session_rows.h"
#include "app/held_transfer.h"
#include "app/ladder_cell.h"
#include "app/doorbell_cell.h"
#include "app/session_tally.h"
#include "app/diag_counters.h"
#include "app/event.h"
#include "app/fabric_state.h"
#include "app/quiesce_ack.h"
#include "app/identity_latch.h"
#include "app/identity_record.h"
#include "app/pending_load.h"
#include "app/fenced_core_signals.h"
#include "app/link_binder.h"
#include "app/link_router.h"
#include "app/session_bindings.h"
#include "app/link_tx_channel.h"
#include "app/session_machine.h"
#include "app/start_in_flight.h"
#include "app/spi_encoder.h"
#include "proto/save_ask.h"
#include "app/save_flush.h"
#include "app/nvram_upload.h"
#include "proto/osd_focus.h"
#include "proto/session_live.h"
#include "proto/spi_osd_mask_decoder.h"
#include "proto/spi_sampler.h"
#include "proto/user_io/poll_reader.h"
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
#include "infra/log_lane.h"
#include "infra/telemetry.h"
#include "infra/seat.h"
#include "infra/opt_ref.h"
#include "os/clock.h"

#include "reactor/executive.h"
#include "reactor/link_decoder.h"
#include "hal/spi_transport.h"
#include "proto/core_session.h"
#include "proto/types.h"
#include "svc/joy_plan.h"

namespace mister::svc {
class DiscReadService;
struct ConfigSnapshot;
class Vfs;
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
class InputWire;
class DurableWriteService;
class MailboxRelay;
class DiskRelay;
class SaveUploadRelay;
class WindowJobService;
template <SeatTag Consumer>
class LinkRxChannel;

struct SupervisorParts;

class LinkSession : private cores::CoreInitHost,
                    private ISaveFlush,
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

    proto::CoreSession& session() noexcept { return session_; }

    [[nodiscard]] const CoreTenancy& tenancy() const noexcept { return tenancy_; }

    [[nodiscard]] const HeldTransfer& held() const noexcept { return held_; }

    [[nodiscard]] const SlotBindings& slot_bindings() const noexcept { return slot_bindings_; }

    [[nodiscard]] const CoreOptionCells& option_cells() const noexcept { return options_; }

    [[nodiscard]] const SessionRows& rows() const noexcept { return rows_; }

    [[nodiscard]] const proto::user_io::PollReader& f12_mod_poll() const noexcept {
        return f12_mod_poll_;
    }

    [[nodiscard]] const hal::PinLevelCell& pin_level_cell() const noexcept {
        return sampler_.levels();
    }

    const proto::SpiOsdMaskDecoder& osd_mask_decoder() const noexcept { return osd_mask_decoder_; }

    [[nodiscard]] svc::DiscReadService& discs() noexcept { return discs_; }

    [[nodiscard]] const CoreTerms& core_terms() const noexcept { return terms_; }

    [[nodiscard]] CoreBay& core_bay() noexcept { return tenancy_.bay(); }

    [[nodiscard]] const BayReleasesCell& bay_releases() const noexcept {
        return tenancy_.releases();
    }

    [[nodiscard]] LinkTxChannel<SeatTag::Main>& link_inbox() noexcept { return link_inbox_; }

    [[nodiscard]] LinkTxChannel<SeatTag::Ui>& ui_inbox() noexcept { return ui_inbox_; }

    [[nodiscard]] LinkTxChannel<SeatTag::Input>& input_inbox() noexcept { return input_inbox_; }

    [[nodiscard]] LinkRxChannel<SeatTag::Main>& link_rx() noexcept { return link_rx_; }

    [[nodiscard]] const xthread::Telemetry<svc::ConfigSnapshot, SeatTag::Main>&
    config_cell() noexcept {
        return config_cell_;
    }

    [[nodiscard]] IdentityRecordCell& identity_record_cell() noexcept {
        return identity_record_cell_;
    }

    [[nodiscard]] std::uint8_t last_download_index() const noexcept {
        return held_.last_download_index();
    }

    [[nodiscard]] proto::LateAnswers take_late_answers() noexcept {
        return slot_bindings_.slots().take_late_answers();
    }

    [[nodiscard]] bool wire_held() const noexcept { return held_.wire_held(); }

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
    Result on(const proto::LinkOp::BindDoorbells& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::DropCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AdoptCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SessionUp& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SetVolume& a, const LinkOpCtx& ctx) noexcept;
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

    [[nodiscard]] svc::IIoCoworker& stream_coworker() noexcept { return streams_; }

    void seal_boot_pump() noexcept;

    [[nodiscard]] DiskRelay& disks() noexcept { return disks_; }

    [[nodiscard]] SaveUploadRelay& saves() noexcept { return saves_; }

    void settle_boot_slot_mount() noexcept;

    [[nodiscard]] const FileStreamService& streams() const noexcept { return streams_; }

    [[nodiscard]] FileStreamService& streams() noexcept { return streams_; }

    void settle_writes() noexcept { nvram_.settle_writes(); }

    [[nodiscard]] NvramUpload::Tally nvram_tally() const noexcept { return nvram_.tally(); }

    using Identity = SessionIdentity;

    [[nodiscard]] bool copy_identity(Identity& out) const noexcept { return identity_.copy(out); }

    [[nodiscard]] const IdentityLatch& identity() const noexcept { return identity_; }

    void diag_counters(DiagCounters& out) const noexcept;

    [[nodiscard]] const DiagCountersCell& diag_cell() const noexcept { return diag_; }

    [[nodiscard]] const proto::StatusCell& status_cell() const noexcept {
        return rows_.status_cell();
    }

    [[nodiscard]] const DipCell& dip_cell() const noexcept { return options_.dip_cell(); }

    [[nodiscard]] const ConfigSlotCell& config_slot_cell() const noexcept {
        return options_.config_slot_cell();
    }

    [[nodiscard]] const OptionCell& option_cell() const noexcept { return options_.option_cell(); }

    [[nodiscard]] const MountStatusCell& mount_status_cell() const noexcept {
        return slot_bindings_.mount_status_cell();
    }

    [[nodiscard]] const SaveExtentCell& save_extent_cell() const noexcept {
        return slot_bindings_.save_extent_cell();
    }

    proto::BlockSlots& block_slots() noexcept { return slot_bindings_.slots(); }

    const SaveImageSource& save_source() const noexcept { return slot_bindings_.save_source(); }

    [[nodiscard]] LinkBinder& binder() noexcept { return binder_; }

    void enable_stdout_routing(
        const char* null_path = nullptr,
        const char* debug_path = nullptr
    ) noexcept {
        boot_.enable_stdout_routing(null_path, debug_path);
    }

    [[nodiscard]] StdoutRouter* boot_stdout_router() noexcept { return boot_.boot_stdout_router(); }

    struct RoundEntry {
        bool live = false;
    };

    [[nodiscard]] RoundEntry poll(bool tick);

    void tick(RoundEntry entry);

    void deliver(const proto::LinkOp& op, const ILinkTxReader& inbox, bool windows_closed) noexcept;

    void reap_storage() noexcept;

    void service_saves(std::uint32_t budget_words) noexcept;

    void poll_osd_mask();

    void publish_doorbell_stats() noexcept;

    [[nodiscard]] const DoorbellStatsCell& doorbell_cell() const noexcept { return doorbell_cell_; }

    [[nodiscard]] Ex<std::size_t> flush_save_upload() {
        return nvram_.flush_save_upload(save_ctx_());
    }

    [[nodiscard]] Ex<std::size_t> save_upload_now() { return nvram_.save_upload_now(save_ctx_()); }

    [[nodiscard]] bool save_upload_counted() noexcept override;

    [[nodiscard]] std::uint32_t core_edge_seq() const noexcept { return core_edge_seq_; }

    [[nodiscard]] infra::OptRef<cores::Core> live_core() noexcept { return tenancy_.core(); }

    [[nodiscard]] Ex<std::optional<UiRequest::LoadCore>> boot(std::span<const char* const> argv);

    [[nodiscard]] ExitReboot handoff_to_alternate_executable(
        const char* exe,
        std::span<const char* const> argv
    ) {
        return boot_.handoff_to_alternate_executable(exe, argv);
    }

    [[nodiscard]] const BoardOps& board_ops() const noexcept { return ops_; }

    [[nodiscard]] const os::IClock& clock() const noexcept { return clock_; }

    [[nodiscard]] bool session_starting() const noexcept { return start_.on(); }

    [[nodiscard]] const proto::ISessionLive& liveness() const noexcept { return *this; }

    [[nodiscard]] Ex<void> load_core(const LoadRequest& req);

    [[nodiscard]] Ex<void> reset_core();

    [[nodiscard]] Ex<ExitReboot> shutdown();

    void release_bus() noexcept;

    bool fpga_unconfigured() const noexcept { return fabric_.unconfigured(); }

    [[nodiscard]] FabricCell& fabric_cell() noexcept { return fabric_cell_; }

    [[nodiscard]] const FabricStateView& fabric() const noexcept { return fabric_; }

    [[nodiscard]] SwitchPark& switch_park() noexcept { return switch_park_; }

    [[nodiscard]] const QuiesceAck& park() const noexcept { return park_; }

    bool ever_running() const noexcept { return ever_running_; }

    const svc::ConfigSnapshot* config() const noexcept { return config_.config(); }

private:
    [[nodiscard]] bool session_live() const noexcept override;

    [[nodiscard]] bool session_live_() const noexcept {
        return machine_.state() == SessionState::Running && !start_.on() && !park_.engaged();
    }

    [[nodiscard]] bool osd_focused() const noexcept override;

    [[nodiscard]] Ex<void> detach();

    [[nodiscard]] Ex<void> raise_session_(const proto::RtcImage& rtc);

    void arm_request(const LoadRequest& req);

    void apply_core_(const proto::LinkOp::ApplyCore& a);

    void adopt_programmed_core_();

    [[nodiscard]] Ex<void> perform_session_up_(const proto::LinkOp::SessionUp& op);

    void refuse_start_(const Error& e) noexcept;

    [[nodiscard]] bool options_push_save_bytes(
        proto::SaveKind kind,
        proto::SaveStatus st,
        proto::RxSlabId path,
        proto::RxSlabId data,
        proto::CorrelationTag tag
    ) noexcept override;
    [[nodiscard]] Ex<proto::RxSlabId> options_intern_save_bytes(
        std::span<const std::uint8_t> bytes
    ) noexcept override;
    [[nodiscard]] bool reboot_push_quiesced(std::uint16_t seq) noexcept override;

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
    svc::DiscReadService& discs_;
    svc::DiscReadService& unbound_discs_;
    MailboxRelay& mailbox_;
    DiskRelay& disks_;
    SaveUploadRelay& saves_;
    NvramUpload nvram_;
    WindowJobService& window_jobs_;
    LinkTxChannel<SeatTag::Main>& link_inbox_;
    LinkTxChannel<SeatTag::Ui>& ui_inbox_;
    LinkTxChannel<SeatTag::Input>& input_inbox_;
    LinkRxChannel<SeatTag::Main>& link_rx_;
    LinkRouter& router_;
    Port out_;
    proto::SpiSampler sampler_;
    const xthread::Telemetry<svc::ConfigSnapshot, SeatTag::Main>& config_cell_;

    IdentityRecordCell& identity_record_cell_;

    BootArm boot_;

    ConfigPair config_;

    StartInFlight start_;
    SessionTally tally_{};

    std::uint32_t disc_sync_decompress_ = 0;
    std::uint32_t disc_park_timeouts_ = 0;
    std::uint32_t disc_prefetch_refusals_ = 0;

    cores::CdFlow cd_flow_{};

    VideoWire& video_;
    InputWire& input_;
    OsdWire& osd_;

    proto::SpiOsdMaskDecoder osd_mask_decoder_;

    IdentityLatch identity_;

    void publish_identity();

    void reset_video_geometry() override;
    void arm_video_prelude() override;
    void publish_core_identity() override;

    const BoardOps ops_;
    RebootAnswer reboot_;

    FabricCell fabric_cell_{};
    FabricStateView fabric_{};
    SwitchPark switch_park_;
    QuiesceAck park_{};

    std::uint32_t dropped_gen_ = 0;
    std::uint32_t holds_at_drop_ = 0;
    std::uint16_t torn_gen_ = 0;
    std::uint16_t torn_done_ = 0;

    void take_park_ask_() noexcept;

    void publish_fabric_();

    bool ever_running_ = false;

    proto::CoreSession session_;
    UartModeController uart_;

    PendingLoad pending_{};

    SessionBindings bindings_{};

    bool core_ready_ = true;

    xthread::LogLane::Producer log_lane_;
    std::uint64_t load_start_ns_ = 0;

    const LadderStateCell& ladder_cell_;

    DiagCountersCell diag_{};

    void gather_diag(DiagCounters& out) const noexcept;
    void publish_diag() noexcept;

    bool osd_rearmed_ = false;
    void on_osd_visibility_edge(bool visible);

    DoorbellStatsCell doorbell_cell_{};
    DoorbellStats doorbell_last_{};

    ICheatApply& cheats_;

    infra::OptRef<const cores::SaveCopyCell> save_copy_;

    CoreOptionCells options_;

    const os::IClock& clock_;

    const CoreTerms terms_;

    std::uint32_t core_edge_seq_ = 0;

    SlotBindings slot_bindings_;

    SessionRows rows_;

    proto::user_io::PollReader f12_mod_poll_;

    CoreTenancy tenancy_;

    FileStreamService& streams_;

    SpiEncoder encoder_;

    HeldTransfer held_;
};

}
