// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "app/file_stream_service.h"
#include "infra/wake_flag.h"
#include "hal/pin_levels.h"
#include "proto/image_bracket.h"
#include "proto/link_event.h"
#include "proto/link_op.h"
#include "proto/link_port.h"
#include "proto/download_session.h"
#include "proto/spi_block_decoder.h"
#include "proto/spi_osd_mask_decoder.h"
#include "proto/spi_conf_str_decoder.h"
#include "proto/decoder_row.h"
#include "proto/spi_identify_decoder.h"
#include "proto/spi_sampler.h"
#include "proto/spi_status_decoder.h"
#include "app/board_ops.h"
#include "app/bracket_close.h"
#include "app/cheat_blob_cell.h"
#include "app/cheat_catalog_cell.h"
#include "app/config_slot_cell.h"
#include "app/ladder_cell.h"
#include "app/file_tx_level.h"
#include "app/mount_status_cell.h"
#include "app/save_extent_cell.h"
#include "app/dip_cell.h"
#include "app/option_cell.h"
#include "app/owed_mount.h"
#include "app/owed_save_bind.h"
#include "app/doorbell_cell.h"
#include "app/diag_counters.h"
#include "app/event.h"
#include "app/fabric_state.h"
#include "app/quiesce_ack.h"
#include "app/identity_latch.h"
#include "app/pending_load.h"
#include "app/fenced_core_signals.h"
#include "app/link_binder.h"
#include "app/link_router.h"
#include "app/session_bindings.h"
#include "app/link_tx_channel.h"
#include "app/session_machine.h"
#include "app/start_in_flight.h"
#include "app/spi_encoder.h"
#include "proto/status_cell.h"
#include "app/tx_digest_cell.h"
#include "proto/save_ask.h"
#include "app/save_flush.h"
#include "app/cheat_apply.h"
#include "app/core_option_acts.h"
#include "proto/session_live.h"
#include "proto/osd_focus.h"
#include "app/save_image_source.h"
#include "app/stdout_router.h"
#include "app/ui_request.h"
#include "app/uart_mode.h"
#include "cores/load_progress.h"
#include "cores/progress_ticker.h"
#include "cores/stream_load_bracket.h"
#include "cores/save_channel.h"
#include "cores/core_init_host.h"
#include "infra/fixed_str.h"
#include "infra/message_sum.h"
#include "infra/error.h"
#include "infra/log_lane.h"
#include "infra/telemetry.h"
#include "infra/seat.h"
#include "svc/config_snapshot.h"
#include "os/clock.h"
#include "os/monotonic_clock.h"

#include "reactor/executive.h"
#include "reactor/link_decoder.h"
#include "hal/spi_transport.h"
#include "proto/block_slots.h"
#include "proto/core_session.h"
#include "proto/types.h"
#include "svc/storage_lifecycle.h"
#include "svc/joy_plan.h"

namespace mister::hal {
class IBootHandoff;
}

namespace mister::svc {
class DiscReadService;
struct ConfigSnapshot;
class Vfs;
class IFile;
class AudioService;
class ChdPrefetch;
class InputEmitter;
}  // namespace mister::svc

namespace mister::cores {
class Core;
class ISaveUpload;
}  // namespace mister::cores

namespace mister::proto {
class ImageBracket;
}

namespace mister::app {

class VideoWire;
class OsdWire;
class InputWire;
class DurableWriteService;
class WindowJobService;
class LinkRxChannel;

struct SupervisorParts;

struct CoreDeleter {
    void operator()(cores::Core* p) const noexcept;
};

class LinkSession : private cores::CoreInitHost,
                    private cores::ILoadProgress,
                    private IBracketClose,
                    private ISaveFlush,
                    private proto::ISessionLive,
                    private proto::IOsdFocus,
                    private ICheatApply,
                    private ICoreOptionActs {
    TASTY_SEAT_RESIDENT(RT);

public:
    LinkSession(hal::ISpiTransport& link, hal::ICoreSignals& signals,
                hal::ISpiSampleSource& samples, hal::PhysRegion aperture, EventQueue& events,
                const SupervisorParts& parts) noexcept;
    ~LinkSession();

    LinkSession(const LinkSession&) = delete;
    LinkSession& operator=(const LinkSession&) = delete;

    using Port = proto::LinkPort<proto::LinkEvent::SaveBytes, proto::LinkEvent::CoreMade,
                                 proto::LinkEvent::StartRefused, proto::LinkEvent::RebootQuiesced>;

    [[nodiscard]] SessionState state() const noexcept { return machine_.state(); }
    proto::CoreSession& session() noexcept { return session_; }

    void attach_storage(const svc::Vfs& vfs) noexcept {
        vfs_ = &vfs;

        save_src_ = std::make_unique<SaveImageSource>(vfs);
        slots_.attach_source(*save_src_);

        streams_.bind_vfs(vfs);
    }

    void attach_executive(reactor::Executive& exec, reactor::CoreState& state) noexcept {
        binder_.attach_executive(exec, state);
    }
    void set_board_ops(const BoardOps& ops) noexcept { ops_ = ops; }

    void set_boot_handoff(hal::IBootHandoff* page) noexcept { handoff_ = page; }

    UartModeController& uart() noexcept { return uart_; }

    void set_doorbell_policy(hal::DoorbellPolicy p) noexcept { binder_.set_doorbell_policy(p); }

    void set_pcm_feeder(PcmRingFeeder* feeder) noexcept { binder_.set_pcm_feeder(feeder); }
    void set_fpga_aperture(hal::FpgaAperture ap) noexcept { binder_.set_fpga_aperture(ap); }

    void set_lw_window(hal::PhysRegion lw) noexcept { binder_.set_lw_window(lw); }

    void attach_clock(const os::IClock& c) noexcept {
        clock_ = &c;
        slots_.attach_clock(c);
    }

    void attach_prefetch(svc::ChdPrefetch& pf) noexcept { prefetch_ = &pf; }

    void attach_discs(svc::DiscReadService& d) noexcept { discs_ = &d; }
    [[nodiscard]] svc::DiscReadService* discs() noexcept { return discs_; }

    void set_log_lane(xthread::LogLane* lane) noexcept {
        log_lane_ = lane;
        machine_.set_log_lane(lane);
        binder_.set_log_lane(lane);
    }

    void set_write_service(DurableWriteService* w) noexcept { writes_ = w; }

    void set_window_job_service(WindowJobService* w) noexcept { window_jobs_ = w; }

    [[nodiscard]] LinkTxChannel& link_inbox() noexcept { return *link_inbox_; }
    [[nodiscard]] LinkTxChannel& ui_inbox() noexcept { return *ui_inbox_; }
    [[nodiscard]] LinkTxChannel& input_inbox() noexcept { return *input_inbox_; }
    [[nodiscard]] LinkRxChannel& link_rx() noexcept { return *link_rx_; }

    [[nodiscard]] const xthread::Telemetry<svc::ConfigSnapshot>& config_cell() noexcept {
        return *config_cell_;
    }
    [[nodiscard]] std::uint32_t link_ops_encoded() const noexcept { return link_ops_encoded_; }
    [[nodiscard]] std::uint32_t link_op_drops() const noexcept { return link_op_drops_; }
    [[nodiscard]] std::uint32_t link_op_misrouted() const noexcept { return link_op_misrouted_; }

    [[nodiscard]] std::uint32_t file_tx_piece_drops() const noexcept { return ftx_piece_drops_; }
    [[nodiscard]] std::uint32_t file_tx_cuts() const noexcept { return ftx_cuts_; }

    [[nodiscard]] std::uint32_t open_saves_mounted() const noexcept { return open_saves_mounted_; }
    [[nodiscard]] std::uint32_t open_save_refusals() const noexcept { return open_save_refusals_; }
    [[nodiscard]] bool file_tx_held() const noexcept { return ftx_open_.has_value(); }
    [[nodiscard]] bool stage_held() const noexcept { return stage_open_.has_value(); }
    [[nodiscard]] std::uint32_t stage_drops() const noexcept { return stage_drops_; }
    [[nodiscard]] std::uint32_t stage_cuts() const noexcept { return stage_cuts_; }

    [[nodiscard]] std::uint32_t downloads_closed() const noexcept { return ftx_count_; }
    [[nodiscard]] std::uint8_t last_download_index() const noexcept {
        return static_cast<std::uint8_t>(ftx_index_);
    }

    [[nodiscard]] proto::LateAnswers take_late_answers() noexcept {
        return slots_.take_late_answers();
    }

    [[nodiscard]] std::uint32_t resets_pulsed() const noexcept { return resets_pulsed_; }

    [[nodiscard]] bool wire_held() const noexcept { return copy_ds_.has_value(); }

    [[nodiscard]] std::uint32_t copy_held_rounds() const noexcept { return copy_rounds_; }

    [[nodiscard]] std::uint32_t save_pulls_fenced() const noexcept { return save_pulls_fenced_; }

    [[nodiscard]] std::uint32_t load_facts_refusals() const noexcept {
        return load_facts_refusals_;
    }

    [[nodiscard]] const FileTxLevelCell& file_tx_level_cell() const noexcept {
        return ftx_level_cell_;
    }

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::BindDecoders& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindSlot& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindFacts& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindConfig& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindUart& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindSlotConfig& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindSlotPreviews& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::RebootNow& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::ApplyCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AbortSwitch& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindIdentity& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindDoorbells& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SaveAsk& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::DropCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::MakeCore& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SessionUp& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StagePayload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageReset& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageDiscPayload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageCheats& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SetVolume& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AnnounceMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SetWideIndex& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::CoreReset& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::LoadFacts& a, const LinkOpCtx& ctx) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    [[nodiscard]] bool declares_turbo() const noexcept override;

    void attach_input_emitter(svc::InputEmitter& em) noexcept { encoder_.attach_joysticks(em); }
    [[nodiscard]] const MraFacts& mra_facts() const noexcept { return bindings_.facts(); }
    [[nodiscard]] const SessionBindings& bindings() const noexcept { return bindings_; }

    [[nodiscard]] bool rebooting() const noexcept { return rebooting_; }
    [[nodiscard]] std::uint32_t stale_owner_steps() const noexcept { return stale_owner_steps_; }
    [[nodiscard]] std::uint32_t start_refused_drops() const noexcept {
        return start_refused_drops_;
    }

    [[nodiscard]] std::uint32_t start_answer_drops() const noexcept {
        return start_refused_drops_ + conf_str_decoder_.lost() + core_made_drops_;
    }

    void set_storage_lifecycle(svc::IStorageLifecycle* l) noexcept {
        storage_life_ = l;
        slots_.set_storage_live(l != nullptr);
    }

    [[nodiscard]] svc::IIoCoworker& stream_coworker() noexcept { return streams_; }
    void seal_boot_pump() noexcept { streams_.seal_boot_pump(); }
    [[nodiscard]] const FileStreamService& streams() const noexcept { return streams_; }
    [[nodiscard]] FileStreamService& streams() noexcept { return streams_; }

    void settle_writes() noexcept;

    using Identity = SessionIdentity;

    [[nodiscard]] bool copy_identity(Identity& out) const noexcept { return identity_.copy(out); }

    [[nodiscard]] const IdentityLatch& identity() const noexcept { return identity_; }

    void diag_counters(DiagCounters& out) const noexcept;

    [[nodiscard]] const DiagCountersCell& diag_cell() const noexcept { return diag_; }

    [[nodiscard]] const proto::StatusCell& status_cell() const noexcept { return status_cell_; }

    [[nodiscard]] const DipCell& dip_cell() const noexcept { return dip_cell_; }

    [[nodiscard]] const hal::PinLevelCell& pin_level_cell() const noexcept {
        return sampler_.levels();
    }

    [[nodiscard]] const ConfigSlotCell& config_slot_cell() const noexcept {
        return config_slot_cell_;
    }

    void set_ladder_cell(LadderStateCell* c) noexcept { ladder_cell_ = c; }

    [[nodiscard]] const MountStatusCell& mount_status_cell() const noexcept {
        return mount_status_cell_;
    }
    [[nodiscard]] const SaveExtentCell& save_extent_cell() const noexcept {
        return save_extent_cell_;
    }
    [[nodiscard]] std::uint32_t config_loads() const noexcept { return config_loads_; }

    [[nodiscard]] const OptionCell& option_cell() const noexcept { return option_cell_; }
    [[nodiscard]] std::uint32_t option_sets() const noexcept { return option_sets_; }

    [[nodiscard]] const TxDigestCell& tx_digest_cell() const noexcept { return tx_digest_cell_; }
    [[nodiscard]] const CheatCatalogCell& cheat_catalog_cell() const noexcept {
        return cheat_catalog_cell_;
    }

    void set_cheat_blob_cell(const CheatBlobCell* c) noexcept { cheat_blob_cell_ = c; }

    [[nodiscard]] bool apply_cheats_counted() noexcept override;

    [[nodiscard]] bool set_dip_counted(std::uint8_t row, std::uint32_t choice) noexcept override;
    [[nodiscard]] bool set_option_counted(std::uint8_t row, std::uint8_t choice) noexcept override;
    [[nodiscard]] bool settle_options_counted() noexcept override;
    [[nodiscard]] std::uint32_t cheat_applies() const noexcept { return cheat_applies_; }
    [[nodiscard]] std::uint32_t cheat_refusals() const noexcept { return cheat_refusals_; }

    [[nodiscard]] std::uint32_t manifest_errors() const noexcept { return manifest_errors_; }

    proto::BlockSlots& block_slots() noexcept { return slots_; }
    const SaveImageSource& save_source() const noexcept { return *save_src_; }

    [[nodiscard]] LinkBinder& binder() noexcept { return binder_; }

    void enable_stdout_routing(const char* null_path = nullptr,
                               const char* debug_path = nullptr) noexcept {
        stdout_armed_ = true;
        if (null_path != nullptr && debug_path != nullptr) {
            stdout_router_.set_paths(null_path, debug_path);
        }
    }

    struct RoundEntry {
        bool live = false;
    };

    [[nodiscard]] RoundEntry poll(bool tick);

    void tick(RoundEntry entry);

    void deliver(const proto::LinkOp& op, LinkTxChannel* inbox, bool windows_closed) noexcept;

    void reap_storage() noexcept;

    void poll_osd_mask();

    void publish_doorbell_stats() noexcept;

    [[nodiscard]] const DoorbellStatsCell& doorbell_cell() const noexcept { return doorbell_cell_; }

    [[nodiscard]] Ex<std::size_t> flush_save_upload();

    [[nodiscard]] Ex<std::size_t> save_upload_now();

    [[nodiscard]] bool save_upload_counted() noexcept override;

    std::uint32_t save_uploads() const noexcept { return save_uploads_; }
    std::uint32_t save_upload_errors() const noexcept { return save_upload_errs_; }

    std::uint32_t osd_close_hooks() const noexcept { return osd_close_hooks_; }

    const proto::SpiOsdMaskDecoder& osd_mask_decoder() const noexcept { return osd_mask_decoder_; }

    std::uint32_t core_shutdowns() const noexcept { return core_shutdowns_; }

    [[nodiscard]] std::uint32_t core_edge_seq() const noexcept { return core_edge_seq_; }

    [[nodiscard]] cores::Core* live_core() noexcept { return current_.get(); }

    [[nodiscard]] bool core_retiring() const noexcept { return retiring_.core != nullptr; }

    [[nodiscard]] std::uint32_t core_abandons() const noexcept { return core_abandons_; }

    [[nodiscard]] Ex<std::optional<UiRequest::LoadCore>> boot(std::span<const char* const> argv);

    [[noreturn]] void handoff_to_alternate_executable(const char* exe,
                                                      std::span<const char* const> argv);

    [[nodiscard]] const BoardOps& board_ops() const noexcept { return ops_; }

    [[nodiscard]] const os::IClock& clock() const noexcept {
        return clock_ != nullptr ? *clock_ : default_clock_;
    }

    [[nodiscard]] bool session_starting() const noexcept { return start_.on(); }

    [[nodiscard]] const proto::ISessionLive& liveness() const noexcept { return *this; }
    [[nodiscard]] Ex<void> load_core(const LoadRequest& req);

    [[nodiscard]] Ex<void> reset_core(bool cold);

    [[nodiscard]] Ex<void> shutdown();

    void release_bus() noexcept;

    bool fpga_unconfigured() const noexcept { return fabric_.unconfigured(); }

    [[nodiscard]] FabricCell& fabric_cell() noexcept { return fabric_cell_; }
    [[nodiscard]] const FabricStateView& fabric() const noexcept { return fabric_; }
    [[nodiscard]] QuiesceChannel& quiesce_channel() noexcept { return quiesce_; }
    [[nodiscard]] const QuiesceAck& park() const noexcept { return park_; }

    bool ever_running() const noexcept { return ever_running_; }

    [[nodiscard]] Ex<void> apply(const svc::ConfigSnapshot& previous,
                                 const svc::ConfigSnapshot& next);

    const svc::ConfigSnapshot* config() const noexcept { return config_store_[config_live_].get(); }

private:
    using CorePtr = std::unique_ptr<cores::Core, CoreDeleter>;

    [[nodiscard]] bool session_live() const noexcept override {
        TASTY_SEAT_BODY(LinkSession);
        return session_live_();
    }

    [[nodiscard]] bool session_live_() const noexcept {
        return machine_.state() == SessionState::Running && !start_.on() && !park_.engaged();
    }

    [[nodiscard]] bool osd_focused() const noexcept override;

    [[nodiscard]] Ex<void> detach();

    [[nodiscard]] Ex<void> raise_session_();

    [[nodiscard]] Ex<void> make_core_(std::string_view manifest, bool hint_manifest);

    void on_load_progress(std::uint16_t cur, std::uint16_t max) noexcept override;

    void before_close() noexcept override;

    bool progress_opened_osd_ = false;

    void mount_save_channel(cores::ISaveChannel& channel);
    void unmount_save_channel();

    static constexpr std::size_t kMaxChannelSlots = proto::kMaxAnnounceableSlot + 1;

    void release_channel_slots_(std::uint16_t mask, std::size_t keep) noexcept;

    [[nodiscard]] Ex<void> apply_bind_slot_(proto::SlotIndex slot, proto::LinkOp::SlotBind kind,
                                            proto::FileSize size, proto::PathId path_id);

    void apply_bind_slot_roles_(proto::SlotIndex slot, proto::IResidentImageSource* resident,
                                proto::IImageSource* descriptor) noexcept;
    void apply_geometry_hook_(proto::IBlockGeometry* hook) noexcept;

    void arm_request(const LoadRequest& req);

    [[nodiscard]] Ex<void> validate_pending() const;

    [[nodiscard]] svc::ConfigSnapshot& live_config_() noexcept;
    [[nodiscard]] svc::ConfigSnapshot& spare_config_() noexcept;
    void promote_spare_() noexcept;

    void take_bound_config_() noexcept;

    [[nodiscard]] Ex<void> re_exec(const LoadRequest&);

    [[nodiscard]] Ex<void> board_reboot(bool cold);

    void apply_core_(const proto::LinkOp::ApplyCore& a);

    [[nodiscard]] Ex<bool> adopt_programmed_core_();

    [[nodiscard]] Ex<void> bind_pre_session_(proto::BindGeneration gen);

    [[nodiscard]] Ex<void> perform_session_up_(const proto::LinkOp::SessionUp& op);

    void refuse_start_(const Error& e) noexcept;

    [[nodiscard]] Ex<void> perform_file_tx_(svc::IFile& f, std::string_view path,
                                            std::uint8_t slot);

    [[nodiscard]] bool stream_file_tx_(const proto::LinkOp::FileTx& a, const FileBytes::Slot* file);

    void after_file_tx_(std::uint8_t slot, std::string_view path);

    void cut_file_tx_() noexcept;

    void publish_ftx_level_(std::uint32_t act, bool open, bool payload = false) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome stage_copy_(const proto::LinkOp::StagePayload& a,
                                                    std::span<const std::uint8_t> bytes) noexcept;

    [[nodiscard]] bool hold_wire_(bool tick) noexcept;
    void end_copy_(bool ok) noexcept;

    [[nodiscard]] bool save_pull_fenced_() noexcept;

    void perform_reboot_();

    void answer_reboot_() noexcept;
    [[nodiscard]] bool io_settled_() const noexcept;
    void sleep_ms(unsigned ms);

    void push(const Event::SessionAdvisory& a);
    void push(const Event::InfoRequest& a);
    void push(const Event::SdActivity& a);
    void push(const Event::ProgressUpdate& a);

    [[nodiscard]] std::string_view conf_str_name() const noexcept;

    bool core_supports_mgl() const noexcept;

    template <class A>
    void log_edge(const A& alt) noexcept;

    [[nodiscard]] Ex<void> rebind_census(std::span<const reactor::LinkDecoderDecl> rows);

    [[nodiscard]] std::span<const reactor::LinkDecoderDecl> census_rows_(
        std::array<reactor::LinkDecoderDecl, kMaxServices>& out) const noexcept;

    hal::ISpiTransport* link_;

    hal::ICoreSignals* signals_;
    FencedCoreSignals fenced_signals_;
    hal::IBootHandoff* handoff_ = nullptr;

    SessionMachine machine_;

    const svc::Vfs* vfs_ = nullptr;
    svc::DiscReadService* discs_ = nullptr;
    svc::ChdPrefetch* prefetch_ = nullptr;

    [[nodiscard]] Ex<void> arm_or_write_(std::string_view rel, std::span<const std::byte> bytes);
    void drain_write_verdicts_();

    void settle_save_lane_();

    [[nodiscard]] Ex<std::size_t> write_save_upload_(cores::ISaveUpload& up);

    DurableWriteService* writes_ = nullptr;
    WindowJobService* window_jobs_ = nullptr;
    svc::IStorageLifecycle* storage_life_ = nullptr;
    LinkTxChannel* link_inbox_ = nullptr;
    LinkTxChannel* ui_inbox_ = nullptr;
    LinkTxChannel* input_inbox_ = nullptr;
    LinkRxChannel* link_rx_ = nullptr;
    LinkRouter& router_;
    Port out_;
    proto::SpiSampler sampler_;
    const xthread::Telemetry<svc::ConfigSnapshot>* config_cell_ = nullptr;
    xthread::Telemetry<svc::ConfigSnapshot>::Reader config_reader_{};

    StartInFlight start_;
    std::uint32_t link_ops_encoded_ = 0;
    std::uint32_t link_op_drops_ = 0;
    std::uint32_t link_op_misrouted_ = 0;

    void answer_save_ask_(const proto::LinkOp::SaveAsk& ask) noexcept;
    [[nodiscard]] bool push_save_bytes_(proto::SaveKind kind, proto::SaveStatus st,
                                        proto::RxSlabId path, proto::RxSlabId data,
                                        CorrelationTag tag) noexcept;
    bool rebooting_ = false;
    bool reboot_answered_ = false;
    std::uint16_t reboot_seq_ = 0;
    std::uint32_t stale_owner_steps_ = 0;
    std::uint32_t start_refused_drops_ = 0;
    std::uint32_t core_made_drops_ = 0;

    void begin_detach_(proto::SlotIndex slot) noexcept;
    void release_save_slots_() noexcept;

    enum class SaveBind : std::uint8_t { Ready, Waiting, Declined };
    [[nodiscard]] Ex<SaveBind> bind_save_(proto::SlotIndex slot, std::string_view path,
                                          bool manual);
    [[nodiscard]] std::uint64_t save_extent_of_(proto::SlotIndex slot) const noexcept;
    std::uint32_t stage_refusals_ = 0;
    std::uint32_t channel_release_refusals_ = 0;

    std::int64_t retire_due_ns_ = 0;
    std::uint64_t retire_t0_ = 0;
    bool retire_logs_ = false;
    bool drop_owed_ = false;
    std::uint32_t core_abandons_ = 0;

    enum class RetireEnd : std::uint8_t { Poll, Round, Adopt, Exit };
    void settle_retire_(RetireEnd end) noexcept;
    void abandon_retiring_(bool counted) noexcept;
    void settle_abandoned_(RetireEnd end) noexcept;

    [[nodiscard]] std::uint16_t poisoned_slots_() const noexcept;
    std::uint32_t prefetch_park_timeouts_ = 0;
    std::uint32_t prefetch_park_deferrals_ = 0;

    std::atomic<std::uint32_t> disc_sync_decompress_{0};
    std::atomic<std::uint32_t> disc_park_timeouts_{0};
    std::atomic<std::uint32_t> disc_prefetch_refusals_{0};

    std::uint32_t cd_flow_waits_ = 0;
    std::uint32_t cd_cdda_sectors_ = 0;
    std::uint32_t cd_data_sectors_ = 0;
    std::uint32_t cd_idle_ticks_ = 0;
    std::uint32_t cd_gated_ticks_ = 0;
    std::uint32_t cd_drive_track_ = 0;
    std::int32_t cd_drive_lba_ = 0;
    std::int32_t cd_drive_audio_lba_ = 0;
    std::uint8_t cd_drive_state_ = 0;
    bool cd_drive_is_data_ = true;
    std::uint32_t cd_substitutes_ = 0;
    std::uint32_t cd_subcode_substitutes_ = 0;
    std::uint32_t cd_busy_ticks_ = 0;
    std::uint32_t cd_egress_abandons_ = 0;
    std::uint32_t cd_not_resident_ = 0;

    VideoWire& video_;
    InputWire& input_;
    OsdWire& osd_;

    IdentityLatch identity_;

    void publish_identity();

    void reset_video_geometry() override;
    void arm_video_prelude() override;
    void publish_core_identity() override;
    BoardOps ops_{};
    bool stdout_armed_ = false;

    FabricCell fabric_cell_{};
    FabricStateView fabric_{};
    QuiesceChannel quiesce_;
    QuiesceAck park_{};

    std::uint32_t dropped_gen_ = 0;
    std::uint32_t holds_at_drop_ = 0;

    void publish_fabric_();

    bool ever_running_ = false;

    proto::CoreSession session_;

    LinkBinder binder_;
    UartModeController uart_{};
    StdoutRouter stdout_router_{};

    PendingLoad pending_{};

    SessionBindings bindings_{};

    proto::StatusWord status_scratch_{};
    proto::StatusCell status_cell_{};

    proto::SpiIdentifyDecoder identify_decoder_;
    proto::DecoderRow<proto::SpiIdentifyDecoder> identify_row_{identify_decoder_};
    proto::SpiStatusDecoder status_decoder_;
    proto::DecoderRow<proto::SpiStatusDecoder> status_row_{status_decoder_};
    proto::SpiConfStrDecoder conf_str_decoder_;
    proto::DecoderRow<proto::SpiConfStrDecoder> conf_str_row_{conf_str_decoder_};

    bool core_ready_ = true;

    xthread::LogLane* log_lane_ = nullptr;
    std::uint64_t load_start_ns_ = 0;

    std::uint32_t ftx_count_ = 0;
    std::uint32_t resets_pulsed_ = 0;
    std::uint64_t ftx_bytes_ = 0;
    std::uint16_t ftx_index_ = 0;
    std::uint32_t ftx_piece_drops_ = 0;
    std::uint32_t ftx_cuts_ = 0;
    std::uint32_t open_saves_mounted_ = 0;
    std::uint32_t open_save_refusals_ = 0;

    struct OpenSave {
        proto::PathId path{};
        proto::FileSize size{};
    };
    std::optional<OpenSave> open_save_;
    void arm_open_save_(const FileBytes::Slot* file, const LinkTxChannel* inbox) noexcept;
    void announce_open_save_() noexcept;
    std::uint32_t save_pulls_fenced_ = 0;
    std::uint32_t load_facts_refusals_ = 0;
    FileTxLevel ftx_level_scratch_{};
    FileTxLevelCell ftx_level_cell_{};

    static constexpr std::size_t kCopyWordsMax = 8;
    std::optional<proto::DownloadSession> copy_ds_{};
    std::array<std::uint16_t, kCopyWordsMax> copy_tail_{};
    std::uint8_t copy_tail_n_ = 0;
    std::uint16_t copy_act_ = 0;
    std::uint32_t copy_rounds_ = 0;

    LadderStateCell* ladder_cell_ = nullptr;

    DiagCountersCell diag_{};

    void gather_diag(DiagCounters& out) const noexcept;
    void publish_diag() noexcept;

    bool osd_rearmed_ = false;

    bool osd_visible_last_ = false;
    void on_osd_visibility_edge(bool visible);
    std::uint32_t save_uploads_ = 0;
    std::uint32_t save_upload_errs_ = 0;
    std::uint32_t osd_close_hooks_ = 0;

    DoorbellStatsCell doorbell_cell_{};
    DoorbellStats doorbell_last_{};

    DipTable dip_scratch_{};
    DipCell dip_cell_{};

    TxDigest tx_digest_scratch_{};
    TxDigestCell tx_digest_cell_{};
    CheatCatalog cheat_catalog_scratch_{};
    CheatCatalogCell cheat_catalog_cell_{};
    const CheatBlobCell* cheat_blob_cell_ = nullptr;
    CheatBlobCell::Reader cheat_blob_reader_{};
    CheatBlob cheat_blob_scratch_{};
    std::uint32_t cheat_applies_ = 0;
    std::uint32_t cheat_refusals_ = 0;
    std::uint32_t manifest_errors_ = 0;
    std::uint32_t dip_sets_ = 0;
    std::uint32_t dip_saves_ = 0;
    void publish_dip_table() noexcept;

    ConfigSlotTable config_slot_scratch_{};
    ConfigSlotCell config_slot_cell_{};
    MountStatusCell mount_status_cell_{};
    SaveExtentCell save_extent_cell_{};

    OwedMount owed_mount_{};

    static constexpr unsigned kOffSlot = SaveImageSource::kSlots;
    OwedSaveBind owed_save_[SaveImageSource::kSlots + 1]{};

    [[nodiscard]] cores::MountStatus mount_body_() noexcept;
    [[nodiscard]] static cores::SaveExtent save_body_(std::uint64_t size_bytes) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome perform_bound_mount_(
        const proto::LinkOp::BindMount& bm, const std::optional<SessionBindings::Mount>& image);
    [[nodiscard]] ILinkEncoder::Outcome apply_save_bind_(const proto::LinkOp::BindSlot& bind,
                                                         LinkTxChannel* inbox) noexcept;

    void settle_save_binds_() noexcept;
    void settle_stage_mount_() noexcept;

    void after_mount_answer_() noexcept;

    [[nodiscard]] bool drain_payload_(std::span<const std::uint8_t> bytes, std::uint16_t chunk,
                                      std::uint64_t at) noexcept;
    [[nodiscard]] ILinkEncoder::Outcome drop_stage_() noexcept;
    [[nodiscard]] bool stage_fits_round_(std::size_t bytes) const noexcept;
    void cut_stage_() noexcept;
    std::uint32_t stage_drops_ = 0;
    std::uint32_t stage_cuts_ = 0;

    cores::ProgressTicker stage_tick_{};
    std::uint32_t config_loads_ = 0;
    void publish_config_slots() noexcept;
    void grant_manifest_(cores::Core& c, std::string_view path);

    OptionTable option_scratch_{};
    OptionCell option_cell_{};
    std::uint32_t option_sets_ = 0;
    void publish_option_table() noexcept;
    void publish_cheat_catalog() noexcept;

    void reset_cheats(TxDigest::Kind kind, std::string_view path, std::uint32_t crc,
                      bool same_game) noexcept;
    void surface_manifest_error();

    void publish_status_word() noexcept;

    Error last_error_{};
    const os::IClock* clock_ = nullptr;
    os::MonotonicClock default_clock_{};

    std::uint32_t core_shutdowns_ = 0;

    std::uint32_t core_edge_seq_ = 0;

    std::unique_ptr<svc::ConfigSnapshot> config_store_[2];

    std::uint8_t config_live_ = 0;

    std::unique_ptr<svc::AudioService> audio_;

    proto::BlockSlots slots_{};
    static_assert(sizeof(proto::BlockSlots) < 4096, "slots_ is an inline member of a "
                                                    "T-RT type; it must stay small");

    proto::SpiBlockDecoder block_decoder_;
    proto::DecoderRow<proto::SpiBlockDecoder> block_row_{block_decoder_};

    proto::SpiOsdMaskDecoder osd_mask_decoder_;
    std::unique_ptr<SaveImageSource> save_src_;

    std::uint16_t channel_slots_ = 0;

    std::uint16_t channel_borrows_ = 0;

    CorePtr current_;

    struct Retiree {
        CorePtr core;
        svc::IStorageLifecycle* seat = nullptr;
        std::uint16_t mask = 0;
        std::uint16_t sent = 0;
    };

    [[nodiscard]] bool poll_retiree_(Retiree& r) noexcept;
    Retiree retiring_{};

    static constexpr std::size_t kMaxAbandonedCores = 4;
    std::array<Retiree, kMaxAbandonedCores> abandoned_{};

    FileStreamService streams_;

    SpiEncoder encoder_;

    std::optional<cores::StreamLoadBracket> ftx_open_{};

    std::optional<proto::ImageBracket> stage_open_{};
};

}  // namespace mister::app
