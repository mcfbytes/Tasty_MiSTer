// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/act_keys.h"
#include "app/bind_tail.h"
#include "app/board_ops.h"
#include "app/board_reboot.h"
#include "app/boot_restore.h"
#include "app/cheat_catalog_cell.h"
#include "app/companion_slot.h"
#include "app/config_saves.h"
#include "app/content_pick.h"
#include "app/core_switch.h"
#include "app/fabric_state.h"
#include "app/fallback_counts.h"
#include "app/identity_record.h"
#include "app/core_ini.h"
#include "app/ini_parse.h"
#include "app/ladder_cell.h"
#include "app/ladder_host_adapter.h"
#include "app/launcher_latch.h"
#include "app/link_rx_channel.h"
#include "app/manifest_ladder.h"
#include "app/mount_queue.h"
#include "app/mount_status_cell.h"
#include "app/negotiated_core.h"
#include "app/owner_ladder.h"
#include "app/owner_tally.h"
#include "app/readiness_watch.h"
#include "app/load_belt.h"
#include "app/main_pass.h"
#include "app/save_extent_cell.h"
#include "app/save_state_attach.h"
#include "app/save_state_facts.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "app/sys_grant.h"
#include "app/sys_route.h"
#include "app/sector_route.h"
#include "app/switch_phase.h"
#include "app/ui_request_ring.h"
#include "app/window_tally.h"
#include "cores/boot_ladder.h"
#include "cores/companion_load.h"
#include "infra/log_lane.h"
#include "os/monotonic_clock.h"
#include "proto/status_cell.h"

namespace mister::app {

class SessionOwner : private LoadBelt::Host,
                     private MountQueue::Host,
                     private ContentPick::Host,
                     private CoreSwitch::Host,
                     private ReadinessWatch::Host,
                     private LadderHostAdapter::Acts {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Link {
        LinkTxChannel<SeatTag::Main>& inbox;
        LinkRxChannel<SeatTag::Main>& rx;
        ConfigCell& config_cell;

        IdentityRecordCell& identity_record_cell;
        EventQueue<SeatTag::Main>& owner_events;
        ConfStrCell& conf_str_cell;
        CheatCatalogCell& cheat_catalog_cell;
        LadderStateCell& ladder_cell;
        xthread::WakeFlag& main_wake;
        BoardOps ops{};
        infra::OptRef<hal::IBootHandoff> boot_handoff{};
        infra::OptRef<LauncherDemandCell> launcher_demand{};
        infra::OptRef<xthread::WakeFlag> launcher_wake{};
        BitstreamProgrammer& programmer;
        const svc::Vfs& storage;
        ILoadWindowMap<seat_owner::Main>& window_map;
        FabricCell& fabric_cell;
        const proto::StatusCell& status_cell;
        const MountStatusCell& mount_status_cell;
        const SaveExtentCell& save_extent_cell;
        const FileTxLevelCell& file_tx_level_cell;
        const hal::PinLevelCell& pin_levels;
        infra::OptRef<const os::IClock> clock{};
        infra::OptRef<SaveStateAttach::AskInbox> save_state_asks{};
        infra::OptRef<const SaveStateFactsCell> save_state_facts{};
        CoreBay& bay;
        const BayReleasesCell& bay_releases;
        const CoreTerms& core_terms;
        infra::OptRef<hal::IDoorbellSource> doorbell_source{};
        infra::OptRef<CompanionHost::Binds> companion_binds{};

        infra::OptRef<SectorGrantCell> sector_grant{};
        infra::OptRef<const SectorRouteCell> sector_route{};

        infra::OptRef<OsdGrantCell> osd_grant{};
        infra::OptRef<const OsdRouteCell> osd_route{};

        infra::OptRef<SysGrantCell> sys_grant{};
        infra::OptRef<const SysRouteCell> sys_route{};

        infra::OptRef<xthread::WakeFlag> io_wake{};
        infra::OptRef<xthread::MainLogLane> log_lane{};
        hal::PhysRegion aperture{os::PhysAddr{0}, 0, nullptr};
    };
    template <class T>
    using Cell = xthread::Telemetry<T, SeatTag::Main>;
    using Count = Cell<std::uint32_t>;

    SessionOwner(SwitchPark& park, const Link& link) noexcept;
    SessionOwner(const SessionOwner&) = delete;
    SessionOwner& operator=(const SessionOwner&) = delete;

    [[nodiscard]] const ReadinessWatch& readiness() const noexcept { return readiness_; }

    [[nodiscard]] const CoreSwitch& core_switch() const noexcept { return switch_; }

    [[nodiscard]] const NegotiatedCore& negotiated() const noexcept { return negotiated_; }

    [[nodiscard]] const MountQueue& mounts() const noexcept { return mounts_; }

    [[nodiscard]] const ContentPick& content() const noexcept { return content_; }

    [[nodiscard]] BoardReboot& board_reboot() noexcept { return reboot_; }

    [[nodiscard]] const BoardReboot& board_reboot() const noexcept { return reboot_; }

    static constexpr std::uint32_t kStartBoundMs = 5000;

    void set_identity_poll_budget(std::uint32_t n) noexcept;
    void set_start_bound_ms(std::uint32_t ms) noexcept;
    void set_readiness_poll_budget(std::uint32_t n) noexcept;

    bool set_front_end_image(std::string_view rel) noexcept { return front_end_image_.assign(rel); }

    void set_replay_save_root(std::string_view rel) noexcept;

    using RememberedFiles = MountQueue::RememberedFiles;

    void set_remembered_files(RememberedFiles r) noexcept { remembered_files_ = r; }

    void arm_replay_ini(bool strict) noexcept;

    void set_boot_cookie(std::uint16_t c);

    void latch_boot_companion(std::string_view xml) noexcept;

    void grant_pause(const hal::ThreadRole& row, xthread::PauseLatch& latch) noexcept;

    void service_boot(const UiRequest::LoadCore& req);

    void expect_boot_negotiate() noexcept;

    void on_seats_live() noexcept;

    unsigned tick();

    [[nodiscard]] int wake_hint_ms(int floor_ms) const noexcept;

    [[nodiscard]] bool program_owed() const noexcept;

    [[nodiscard]] bool rung_pending() const noexcept;

    void on(const UiRequest::LoadCore& req, const UiRequest::Head& head);
    void on(const UiRequest::SaveConfig& req, const UiRequest::Head& head);
    void on(const UiRequest::SaveDips& req, const UiRequest::Head& head);
    void on(const UiRequest::SaveCoreConfig& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadCoreConfig& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadFile& req, const UiRequest::Head& head);
    void on(const UiRequest::MountImage& req, const UiRequest::Head& head);
    void on(const UiRequest::UnmountImage& req, const UiRequest::Head& head);
    void on(const UiRequest::ResetCore& req, const UiRequest::Head& head);
    void on(const UiRequest::Reboot& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadFileByDigit& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadRamImage& req, const UiRequest::Head& head);
    void on(const UiRequest::SetOsdStyle& req, const UiRequest::Head& head);
    void on(const UiRequest::SdramDetected& req, const UiRequest::Head& head);
    void on(const UiRequest::SetHdLook& req, const UiRequest::Head& head);
    void misrouted(const UiRequest& m) noexcept;

    void on(const proto::LinkEvent::ReadyEdge& e) noexcept;
    void on(const proto::LinkEvent::BlockRequest& e) noexcept;
    void on(const proto::LinkEvent::IdentityMatched& e) noexcept;
    void on(const proto::LinkEvent::IdentityMismatch& e) noexcept;
    void on(const proto::LinkEvent::ConfStr& e);
    void on(const proto::LinkEvent::SaveBytes& e);
    void on(const proto::LinkEvent::CoreMade& e) noexcept;
    void on(const proto::LinkEvent::StartRefused& e) noexcept;
    void on(const proto::LinkEvent::RebootQuiesced& e);
    void misrouted(const proto::LinkEvent& m) noexcept;

    [[nodiscard]] LinkTxChannel<SeatTag::Main>& inbox() noexcept;
    [[nodiscard]] const LinkTxChannel<SeatTag::Main>& inbox() const noexcept;

    [[nodiscard]] LinkRxChannel<SeatTag::Main>& rx() noexcept { return rx_; }

    [[nodiscard]] const LinkRxChannel<SeatTag::Main>& rx() const noexcept { return rx_; }

    [[nodiscard]] ConfigCell& config_cell() noexcept { return ini_.cell(); }

    [[nodiscard]] UiRequestRing& ui_requests() noexcept { return ui_requests_; }

    [[nodiscard]] bool park_acks_empty() const noexcept { return !switch_.receipt_owed(); }

    [[nodiscard]] bool boot_config_parsed() const noexcept { return ini_.boot_parsed(); }

    [[nodiscard]] const std::string& boot_handoff() const noexcept { return ini_.boot_handoff(); }

    [[nodiscard]] infra::OptRef<const LauncherProfile> launcher() const noexcept;

    void retire_lw_at_exit() noexcept;

    [[nodiscard]] const CoreBuilder& core_builder() const noexcept { return builder_; }

    [[nodiscard]] const LadderStateCell& ladder_cell() const noexcept { return ladder_.cell(); }

    [[nodiscard]] const OwnerTally& tally() const noexcept { return tally_; }

    [[nodiscard]] const Count& pause_expiries_cell() const noexcept;
    [[nodiscard]] const Count& recover_polls_cell() const noexcept;

    [[nodiscard]] const Count& mgl_row0_cell() const noexcept { return mgl_row0_cell_; }

    [[nodiscard]] const Count& save_write_failures_cell() const noexcept {
        return saves_.save_write_failures_cell();
    }

    [[nodiscard]] const Count& ini_write_failures_cell() const noexcept {
        return saves_.ini_write_failures_cell();
    }

    [[nodiscard]] const Cell<FallbackCounts>& fallback_cell() const noexcept;

    [[nodiscard]] const FallbackCounts& fallbacks() const noexcept { return fallbacks_; }

    [[nodiscard]] const Cell<LoadWindowCounts>& window_cell() const noexcept {
        return windows_.cell();
    }

    [[nodiscard]] const Cell<std::uint8_t>& direct_video_ini_cell() const noexcept {
        return ini_.direct_video_cell();
    }

    [[nodiscard]] bool switch_standing() const noexcept;
    [[nodiscard]] bool reboot_owed() const noexcept;
    [[nodiscard]] const std::optional<proto::ConfStr>& conf_str() const noexcept;
    [[nodiscard]] const RememberedStem& remembered_stem() const noexcept;
    [[nodiscard]] bool ladder_live() const noexcept;

    [[nodiscard]] const phase::SwitchPhase& switch_phase() const noexcept { return phase_; }

private:
    friend struct SessionOwnerTestAccess;
    friend struct SessionOwnerTestDrive;
    using MountAsk = MountQueue::MountAsk;
    using Owed = ContentPick::Owed;
    using Wait = LoadBelt::Wait;

    [[nodiscard]] bool order_walk(
        proto::IoIndex slot,
        std::string_view path,
        std::uint32_t gen
    ) override;
    [[nodiscard]] bool order_savestates(std::string_view image, std::uint32_t gen) override;

    [[nodiscard]] cores::ILadderHost& ladder_host() noexcept override;

    void step_held(RungSlot slot) override;
    [[nodiscard]] Wait held_wait(RungSlot slot) const noexcept override;
    [[nodiscard]] Wait rung_wait_() const noexcept;
    void content_loaded() override;
    void addons_closed() override;
    void last_piece_sent(FileTxPieces& pieces) override;
    void manifest_failed(Errc why) override;
    void manifest_done() override;

    void offer_(BeltOrders&& orders);

    [[nodiscard]] bool arm_loader(
        const ContentRequest& req,
        std::string_view save,
        std::uint64_t save_bytes
    ) override;
    void perform_content(const ContentRequest& req) override;
    void launch_content(const ContentRequest& cr) override;
    void refuse_busy(UiRequest::Kind asked, CorrelationTag tag) noexcept override;
    [[nodiscard]] bool arm_open_save(
        const ContentRequest& cr,
        UiRequest::SaveChoice choice,
        CorrelationTag tag
    ) override;

    void finish_pick_(UiRequest::Kind asked, CorrelationTag tag, ContentPick::Out&& out);
    void run_load_(const PickedLoad& load);
    void after_content_(ContentPick::After&& after);
    void publish_refusal(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept;
    void publish_done(UiRequest::Kind which, CorrelationTag tag) noexcept;
    void refuse_file_ask(UiRequest::Kind asked, CorrelationTag tag, Errc code) noexcept;
    [[nodiscard]] std::optional<std::uint32_t> attach_row_savestates(
        bool opensave,
        std::string_view rom
    );
    void hold_for_savestates(const PickedLoad& load, std::uint32_t gen, std::int64_t due_ns);
    void begin_companion(std::string_view path);

    void switch_minted(const UiRequest::LoadCore& req) noexcept override;
    void drop_core_acts() override;
    void forget_core() noexcept override;
    void programmed(bool ok) override;
    void pause_wedged() noexcept override;
    [[nodiscard]] bool ask_recovery_switch() override;
    void parkless_teardown() override;
    void fabric_suspect() noexcept override;

    [[nodiscard]] bool launcher_swap_held_() const noexcept;
    [[nodiscard]] bool launcher_swap_room_() const noexcept;
    [[nodiscard]] bool order_reset_(
        const proto::LinkOp::CoreReset& op,
        CorrelationTag tag
    ) noexcept;
    [[nodiscard]] std::optional<PathText> open_save_path_(
        std::string_view rom,
        UiRequest::SaveChoice choice
    );
    [[nodiscard]] bool prime_replay_save_(std::string_view path, bool seeded);
    void step_ladder_();
    void bios_missing_();
    void finish_ladder_(const OwnerLadder::Ended& end);
    void drop_ladder_();
    void note_memsz_(const UiRequest::LoadCore& req) noexcept;
    void release_and_await_identity_(std::uint32_t gen) noexcept;
    void order_bind_decoders_(proto::LinkOp::DecoderTable table, std::uint32_t gen) noexcept;
    void order_hold_reset_() noexcept;
    void forget_core_() noexcept;

    [[nodiscard]] std::optional<unsigned> run_(MainPassStep::Step step);
    void program_();
    void swap_launcher_();
    void watch_readiness_();
    void watch_start_() noexcept;

    void watch_bridge_() noexcept;
    void end_bridge_session_() noexcept;
    void give_up_start_(Errc why, phase::Stimulus on) noexcept;

    [[nodiscard]] bool owe_front_end_(const UiRequest::LoadCore& pending) noexcept;
    [[nodiscard]] bool owe_front_end_always_(const UiRequest::LoadCore& pending) noexcept;
    void fall_back_to_front_end_();
    [[nodiscard]] bool refuse_in_load_(UiRequest::Kind kind, CorrelationTag tag) noexcept;

    template <class A>
    [[nodiscard]] bool admit_(const A& req, CorrelationTag tag) noexcept;
    void step_companion_();
    [[nodiscard]] bool ask_savestates_(std::string_view rom, std::uint32_t gen) noexcept;
    [[nodiscard]] bool savestates_answered_(std::uint32_t gen) const noexcept;
    template <class A>
    void order_(const A& alt) noexcept;
    template <class A>
    bool order_refusable_(const A& alt) noexcept;
    void refuse_started_switch_(Errc why, phase::Stimulus on) noexcept;
    void order_session_up_() noexcept;

    [[nodiscard]] bool arm_restore_ladder_(std::string_view config);
    [[nodiscard]] StartChain::Facts start_facts_(
        std::string_view rbf,
        bool index0_taken,
        bool slot0_held,
        bool remembered,
        bool ladder
    ) const;
    [[nodiscard]] std::string start_rbf_() const;
    void order_start_chain_(const StartChain& chain);
    void order_rbf_tail_(std::string_view path, proto::WideIoIndex dest, std::uint64_t offset);

    struct RememberedFile {
        proto::ConfStrFileRow row;
        std::string path;
    };

    [[nodiscard]] std::vector<RememberedFile> read_remembered_files_() const;
    void order_remembered_files_(std::span<const RememberedFile> files);
    void publish_fabric_();
    void step_savestate_hold_();
    [[nodiscard]] bool content_ordered_() const noexcept;
    void order_make_core_(bool fabric, bool identity_bound, const svc::ConfigSnapshot& cfg);
    [[nodiscard]] std::string_view make_core_rel_() const noexcept;
    [[nodiscard]] bool build_has_manifest_(const proto::ConfStr& cs) const;
    [[nodiscard]] bool arm_manifest_();
    void abort_start_(Errc why) noexcept;
    void finish_core_made_() noexcept;
    enum class SlotAsker : std::uint8_t { Load, Boot, Refresh };
    void on_slot_names_(const proto::LinkEvent::SaveBytes& e);
    void refuse_slot_names_(SlotAsker who, CorrelationTag tag);
    void preview_slot_names_(std::span<const std::uint8_t> pack);
    void bind_slot_answer_(bool boot, CorrelationTag tag, std::string_view path);
    void finish_boot_restore_() noexcept;

    void phase_edge_(phase::Stimulus on, Errc why = Errc::negotiation) noexcept;
    void phase_after_watch_(bool reboot) noexcept;

    void judge_(const phase::SwitchPhase::Taken& t, phase::Stimulus on) noexcept;

    [[nodiscard]] bool rebooting_() const noexcept;
    void end_rebooting_switch_() noexcept;
    void refuse_rebooting_(phase::Stimulus on) noexcept;
    void log_conf_str_(const proto::LinkEvent::ConfStr& c) noexcept;
    [[nodiscard]] std::uint64_t log_now_ns_() const noexcept;

    [[nodiscard]] bool failed_apply_() const noexcept;

    SwitchPark& park_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    LinkRxChannel<SeatTag::Main>& rx_;
    IdentityRecordCell& identity_record_cell_;
    CoreIni ini_;
    EventQueue<SeatTag::Main>::Producer owner_events_;
    CheatCatalogCell& cheat_catalog_cell_;
    UiRequestRing ui_requests_;
    const proto::StatusCell& status_cell_;
    const MountStatusCell& mount_status_cell_;
    const SaveExtentCell& save_extent_cell_;
    os::MonotonicClock default_clock_{};
    const os::IClock& clock_;
    std::uint32_t identity_poll_budget_ = 0;
    std::uint32_t start_bound_ms_ = kStartBoundMs;

    bool before_seats_ = false;

    bool front_end_armed_ = false;
    FallbackCounts fallbacks_{};
    Cell<FallbackCounts> fallback_cell_{};
    std::optional<proto::LinkOp::CoreReset> reset_owed_{};
    OwnerTally tally_{};
    FixedStr<64, StrFit::Reject> replay_save_root_{};
    RememberedFiles remembered_files_ = RememberedFiles::AsStock;

    ActKeys act_keys_{};

    IdentityRecord::Stamp identity_stamp_{};
    PathText front_end_image_{};
    BitstreamProgrammer& programmer_;
    ReadinessWatch readiness_;
    const svc::Vfs& vfs_;
    OwnerLadder ladder_;
    NegotiatedCore negotiated_;
    MountQueue mounts_;
    LauncherLatch launcher_;
    FabricCell& cell_;
    std::uint16_t cookie_ = 0;
    std::uint32_t fabric_gen_ = 0;
    const hal::PhysRegion aperture_;
    BootRestore boot_restore_ = BootRestore::None;
    const BoardOps ops_{};
    BindTail bind_;
    ConfigSaves saves_;
    infra::OptRef<hal::IBootHandoff> handoff_{};
    Count mgl_row0_cell_{};

    struct MadeFacts {
        bool restore_owed = false;
        bool cheat_records = false;
    };

    std::optional<MadeFacts> made_{};
    CompanionSlot companion_;
    ILoadWindowMap<seat_owner::Main>& window_map_;
    const FileTxLevelCell& ftx_level_cell_;
    WindowTally windows_{};
    LoadBelt belt_;
    ContentPick content_;
    infra::OptRef<SaveStateAttach::AskInbox> save_state_asks_;
    infra::OptRef<const SaveStateFactsCell> save_state_facts_;
    LadderHostAdapter ladder_host_;
    CoreBuilder builder_;
    BoardReboot reboot_;
    CoreSwitch switch_;
    PathText boot_companion_{};
    phase::SwitchPhase phase_{};
    phase::Stimulus ask_cause_ = phase::Stimulus::LoadCore;
    std::optional<xthread::MainLogLane::Producer> log_{};
};

}
