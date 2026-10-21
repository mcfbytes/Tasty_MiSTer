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
#include "app/board_ops.h"
#include "app/board_reboot.h"
#include "app/cheat_catalog_cell.h"
#include "app/content_pick.h"
#include "app/core_switch.h"
#include "app/fabric_state.h"
#include "app/core_ini.h"
#include "app/ini_parse.h"
#include "app/ladder_cell.h"
#include "app/launcher_latch.h"
#include "app/link_rx_channel.h"
#include "app/manifest_ladder.h"
#include "app/mount_queue.h"
#include "app/mount_status_cell.h"
#include "app/negotiated_core.h"
#include "app/owner_ladder.h"
#include "app/owner_tally.h"
#include "app/readiness_watch.h"
#include "app/rung_roster.h"
#include "app/save_extent_cell.h"
#include "app/save_state_attach.h"
#include "app/save_state_facts.h"
#include "app/start_watch.h"
#include "app/ui_request_ring.h"
#include "app/window_tally.h"
#include "cores/boot_ladder.h"
#include "cores/companion_load.h"
#include "os/monotonic_clock.h"
#include "proto/status_cell.h"

namespace mister::app {

class SessionOwner : private RungRoster::Host,
                     private MountQueue::Host,
                     private ContentPick::Host,
                     private CoreSwitch::Host,
                     private ReadinessWatch::Host,
                     private virtual cores::ILadderHost {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Link {
        LinkTxChannel<SeatTag::Main>& inbox;
        LinkRxChannel<SeatTag::Main>& rx;
        ConfigCell& config_cell;
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
        hal::PhysRegion aperture{os::PhysAddr{0}, 0, nullptr};
    };
    template <class T>
    using Cell = xthread::Telemetry<T, SeatTag::Main>;
    using Count = Cell<std::uint32_t>;

    SessionOwner(QuiesceChannel& park, const Link& link) noexcept;
    SessionOwner(const SessionOwner&) = delete;
    SessionOwner& operator=(const SessionOwner&) = delete;

    [[nodiscard]] const StartWatch& start_watch() const noexcept { return start_watch_; }

    [[nodiscard]] const ReadinessWatch& readiness() const noexcept { return readiness_; }

    [[nodiscard]] const CoreSwitch& core_switch() const noexcept { return switch_; }

    [[nodiscard]] const NegotiatedCore& negotiated() const noexcept { return negotiated_; }

    [[nodiscard]] const MountQueue& mounts() const noexcept { return mounts_; }

    [[nodiscard]] const ContentPick& content() const noexcept { return content_; }

    [[nodiscard]] BoardReboot& board_reboot() noexcept { return reboot_; }

    [[nodiscard]] const BoardReboot& board_reboot() const noexcept { return reboot_; }

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

    void grant_pause(SeatTag s, xthread::PauseLatch& latch) noexcept;

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

    [[nodiscard]] bool park_acks_empty() const noexcept { return park_.ack_empty(); }

    [[nodiscard]] bool boot_config_parsed() const noexcept { return ini_.boot_parsed(); }

    [[nodiscard]] const std::string& boot_handoff() const noexcept { return ini_.boot_handoff(); }

    [[nodiscard]] infra::OptRef<const LauncherProfile> launcher() const noexcept;

    [[nodiscard]] const CoreBuilder& core_builder() const noexcept { return builder_; }

    [[nodiscard]] const LadderStateCell& ladder_cell() const noexcept { return ladder_.cell(); }

    [[nodiscard]] const OwnerTally& tally() const noexcept { return tally_; }

    [[nodiscard]] const Count& pause_expiries_cell() const noexcept;
    [[nodiscard]] const Count& recover_polls_cell() const noexcept;

    [[nodiscard]] const Count& mgl_row0_cell() const noexcept { return mgl_row0_cell_; }

    [[nodiscard]] const Count& save_write_failures_cell() const noexcept { return swf_cell_; }

    [[nodiscard]] const Count& ini_write_failures_cell() const noexcept { return iwf_cell_; }

    [[nodiscard]] const Cell<FallbackCounts>& fallback_cell() const noexcept;

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

private:
    friend struct SessionOwnerTestAccess;
    using MountAsk = MountQueue::MountAsk;
    using PickedLoad = ContentPick::PickedLoad;
    using Owed = ContentPick::Owed;
    using Wait = RungRoster::Wait;

    [[nodiscard]] bool order(const proto::LinkOp& op) override;
    using cores::ILadderHost::order;
    [[nodiscard]] Ex<proto::FileId> intern_bytes(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        std::uint32_t load_addr
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_path(
        std::string_view path,
        std::uint64_t size_bytes
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_piece(
        std::span<const std::uint8_t> bytes,
        std::string_view ext,
        std::uint64_t whole,
        std::uint64_t offset
    ) override;
    [[nodiscard]] Ex<proto::FileId> intern_whole(
        std::vector<std::uint8_t>&& bytes,
        std::string_view path
    ) override;
    [[nodiscard]] std::optional<bool> file_tx_answer(std::uint32_t act) override;
    [[nodiscard]] bool order_load_facts(std::span<const std::uint8_t> facts) override;
    [[nodiscard]] std::size_t link_in_flight() const noexcept override;
    [[nodiscard]] std::size_t link_word_bytes() const noexcept override;
    [[nodiscard]] std::uint32_t next_generation() noexcept override;
    [[nodiscard]] cores::MountStatus mount_status() override;
    [[nodiscard]] bool order_walk(
        proto::IoIndex slot,
        std::string_view path,
        std::uint32_t gen
    ) override;
    [[nodiscard]] cores::MountStatus walk_status() override;
    [[nodiscard]] bool order_savestates(std::string_view image, std::uint32_t gen) override;
    [[nodiscard]] std::uint32_t savestates_answered() override;
    [[nodiscard]] cores::SaveExtent save_extent() override;
    [[nodiscard]] proto::StatusWord status_word() override;

    [[nodiscard]] infra::OptRef<Rung> rung(RungSlot slot) noexcept override;
    [[nodiscard]] infra::OptRef<const Rung> rung(RungSlot slot) const noexcept override;
    template <class Self>
    [[nodiscard]] static auto rung_in_(Self& self, RungSlot slot) noexcept;
    void settle(RungSlot slot, Rung::Pass pass, std::uint32_t pushed) override;
    void step_held(RungSlot slot) override;
    [[nodiscard]] Wait held_wait(RungSlot slot) const noexcept override;
    [[nodiscard]] Wait rung_wait_() const noexcept;

    [[nodiscard]] bool arm_loader(
        const ContentRequest& req,
        std::string_view save,
        std::uint64_t save_bytes
    ) override;
    void perform_content(const ContentRequest& req) override;
    void launch_content(const ContentRequest& cr) override;
    [[nodiscard]] bool arm_open_save(
        const ContentRequest& cr,
        UiRequest::SaveChoice choice,
        CorrelationTag tag
    ) override;

    void finish_pick_(UiRequest::Kind asked, CorrelationTag tag, const ContentPick::Out& out);
    void run_load_(const PickedLoad& load);
    void after_content_(const ContentPick::After& after);
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
    [[nodiscard]] bool ask_recovery_switch() override;
    void parkless_teardown() override;

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
    void watch_start_() noexcept;
    void give_up_start_(Errc why) noexcept;
    void fall_back_to_front_end_();
    void sleep_ms_(unsigned ms);
    bool push_gating_op_(const proto::LinkOp& op);
    [[nodiscard]] bool refuse_in_load_(UiRequest::Kind kind, CorrelationTag tag) noexcept;

    template <class A>
    [[nodiscard]] bool admit_(const A& req, CorrelationTag tag) noexcept;
    void make_companion_();
    void withdraw_companion_() noexcept;
    void step_companion_();
    [[nodiscard]] bool ask_savestates_(std::string_view rom, std::uint32_t gen) noexcept;
    [[nodiscard]] bool savestates_answered_(std::uint32_t gen) const noexcept;
    template <class A>
    void order_(const A& alt) noexcept;
    template <class A>
    bool order_refusable_(const A& alt) noexcept;
    void on_identity_matched_(const proto::LinkEvent::IdentityMatched& w) noexcept;
    void on_identity_mismatch_(const proto::LinkEvent::IdentityMismatch& w) noexcept;
    void on_conf_str_(const proto::LinkEvent::ConfStr& c);
    void refuse_started_switch_(Errc why) noexcept;
    void on_core_made_(const proto::LinkEvent::CoreMade& m) noexcept;
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
    [[nodiscard]] bool order_reset_level_(bool asserted);

    struct RememberedFile {
        proto::ConfStrFileRow row;
        std::string path;
    };

    [[nodiscard]] std::vector<RememberedFile> read_remembered_files_() const;
    void order_remembered_files_(std::span<const RememberedFile> files);
    void order_bind_identity_(const proto::ConfStr& conf);
    void order_bind_doorbells_(const proto::ConfStr& conf);
    void order_bind_joy_(std::string_view core_name) noexcept;
    void publish_fabric_();
    void step_savestate_hold_();
    [[nodiscard]] bool content_ordered_() const noexcept;
    [[nodiscard]] bool intern_rel_at_(proto::FileId id, std::string_view rel);
    void order_slot_previews_(const std::byte* pack, std::size_t len);
    void order_bind_facts_() noexcept;
    void order_make_core_();
    [[nodiscard]] std::string_view make_core_rel_() const noexcept;
    void post_make_core_();
    [[nodiscard]] bool arm_manifest_();
    void finish_core_made_() noexcept;
    void order_bind_uart_(std::string_view core_name, const proto::ConfStr* conf = nullptr);
    void on_save_config_(CorrelationTag tag);
    [[nodiscard]] bool order_save_ask_(
        proto::SaveKind kind,
        std::uint8_t slot,
        CorrelationTag tag,
        bool gating = false
    ) noexcept;
    void on_save_bytes_(const proto::LinkEvent::SaveBytes& e);
    void on_slot_names_(const proto::LinkEvent::SaveBytes& e);
    void finish_boot_restore_() noexcept;
    void order_save_upload_() noexcept;
    void publish_save_verdict_(bool ok, CorrelationTag tag) noexcept;
    [[nodiscard]] bool write_durably_(std::string_view rel, std::span<const std::byte> bytes);

    QuiesceChannel& park_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    LinkRxChannel<SeatTag::Main>& rx_;
    CoreIni ini_;
    EventQueue<SeatTag::Main>& owner_events_;
    CheatCatalogCell& cheat_catalog_cell_;
    UiRequestRing ui_requests_;
    const proto::StatusCell& status_cell_;
    const MountStatusCell& mount_status_cell_;
    const SaveExtentCell& save_extent_cell_;
    os::MonotonicClock default_clock_{};
    const os::IClock& clock_;
    StartWatch start_watch_;
    std::optional<proto::LinkOp::CoreReset> reset_owed_{};
    OwnerTally tally_{};
    FixedStr<64, StrFit::Reject> replay_save_root_{};
    RememberedFiles remembered_files_ = RememberedFiles::AsStock;

    ActKeys act_keys_{};
    std::uint8_t slot_load_slot_ = 0;
    std::uint8_t slot_save_slot_ = 0;
    Count swf_cell_{};
    Count iwf_cell_{};
    bool core_made_once_ = false;
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
    bool boot_restore_owed_ = false;
    bool session_up_owed_ = false;
    const BoardOps ops_{};
    infra::OptRef<hal::IBootHandoff> handoff_{};
    Count mgl_row0_cell_{};
    std::optional<AddonSend> addons_{};
    std::optional<AddonSend> addons_after_{};

    struct SaveStateHold {
        PickedLoad load{};
        std::uint32_t gen = 0;
        std::int64_t due_ns = 0;
    };

    std::optional<SaveStateHold> ss_hold_{};
    std::optional<FileTxPieces> pieces_{};
    std::optional<LoadLadder> load_{};
    std::optional<ManifestLadder> manifest_{};
    bool made_restore_owed_ = false;
    bool made_cheat_records_ = false;
    std::optional<LoadWalk> walk_{};
    std::unique_ptr<cores::ICompanionLoad> companion_{};
    bool companion_attached_ = false;
    std::optional<CompanionWalk> companion_walk_{};
    std::uint16_t companion_gen_ = 0;
    std::uint32_t walk_gen_ = 0;
    cores::MountStatus walk_answer_{};
    std::uint32_t ordered_in_step_ = 0;
    bool walk_answer_fresh_ = false;
    ILoadWindowMap<seat_owner::Main>& window_map_;
    const FileTxLevelCell& ftx_level_cell_;
    WindowTally windows_{};
    ContentPick content_;
    infra::OptRef<SaveStateAttach::AskInbox> save_state_asks_;
    infra::OptRef<const SaveStateFactsCell> save_state_facts_;
    infra::OptRef<CompanionHost::Binds> companion_binds_;
    CoreBuilder builder_;
    BoardReboot reboot_;
    CoreSwitch switch_;
    RungRoster roster_;
    PathText boot_companion_{};
};

}
