// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>

#include "app/content_request.h"
#include "app/event.h"
#include "app/link_tx_channel.h"
#include "app/mirror_fill.h"
#include "app/owner_tally.h"
#include "app/path_text.h"
#include "app/start_chain.h"
#include "app/ui_request.h"
#include "cores/ladder_host.h"
#include "cores/registry.h"
#include "cores/staging_core.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/link_op.h"
#include "svc/card_writer.h"
#include "svc/mount_refusal.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class NegotiatedCore;
class OwnerLadder;

class MountQueue {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct MountAsk {
        UiRequest::Kind which = UiRequest::Kind::MountImage;
        proto::IoIndex index{};
        PathText path{};
        CorrelationTag tag{};
        std::optional<proto::FioIndexByte> index_word{};

        enum class Servant : std::uint8_t { None, Image, Control };
        Servant servant = Servant::None;
    };

    class Host {
    public:
        [[nodiscard]] virtual cores::ILadderHost& ladder_host() noexcept = 0;

        [[nodiscard]] virtual bool arm_loader(
            const ContentRequest& req,
            std::string_view save,
            proto::FileSize save_bytes
        ) = 0;
        virtual void perform_content(const ContentRequest& req) = 0;
        virtual void launch_content(const ContentRequest& cr) = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        virtual ~Host() = default;
    };

    enum class RememberedFiles : std::uint8_t { AsStock, Ignore };

    struct Tally {

        std::uint32_t mounts_armed = 0;
        std::uint32_t mounts_held = 0;
        std::uint32_t mounts_refused = 0;

        std::uint32_t remembered_mounts_armed = 0;
        std::uint32_t remembered_mounts_skipped = 0;

        std::uint32_t boot_images_armed = 0;
        std::uint32_t boot_images_skipped = 0;
        std::uint32_t save_mounts_done = 0;

        infra::Tally mirror_fills{};
        infra::Tally mirror_fills_refused{};
        infra::Tally mirror_fills_skipped{};
    };

    struct Wiring {
        const svc::Vfs& vfs;
        LinkTxChannel<SeatTag::Main>& inbox;
        EventQueue<SeatTag::Main>& events;
        const svc::MountRefusalCell& refusals;
        const os::IClock& clock;
        OwnerLadder& ladder;
        const NegotiatedCore& negotiated;
        OwnerTally& owner_tally;
        ILoadWindowMap<seat_owner::Main>& window_map;
        hal::FpgaAperture aperture;
    };

    explicit MountQueue(const Wiring& w) noexcept;

    MountQueue(const MountQueue&) = delete;
    MountQueue& operator=(const MountQueue&) = delete;

    void on_mount_image(Host& host, const MountAsk& ask);

    void arm_mount(Host& host, const MountAsk& ask);

    void arm_next_mount(Host& host);

    [[nodiscard]] bool read_remembered_mounts(RememberedFiles files);
    void queue_boot_images(const StartChain& chain);

    void step_save_mount(Host& host);

    void arm_load_save(const MountAsk& ask, const ContentRequest& load);

    void drop() noexcept;

    void ladder_ended() noexcept;

    [[nodiscard]] bool save_mount_live() const noexcept { return save_mount_.has_value(); }

    [[nodiscard]] bool mirror_filling() const noexcept {
        return save_mount_ && save_mount_->step == SaveMount::Step::Mirror;
    }

    [[nodiscard]] bool save_holds_load() const noexcept {
        return save_mount_ && save_mount_->then_load;
    }

    [[nodiscard]] bool mount_held() const noexcept { return held_mount_.has_value(); }

    [[nodiscard]] bool idle() const noexcept {
        return !mount_busy_() && !held_mount_.has_value() && !start_mounts_live_();
    }

    [[nodiscard]] std::string_view mounted_path(proto::SlotIndex slot) const noexcept {
        const auto held = mounted_.find(slot);
        return held ? held->view() : std::string_view{};
    }

    [[nodiscard]] bool announced(proto::SlotIndex slot) const noexcept {
        return announced_.contains(slot);
    }

    [[nodiscard]] std::size_t start_mounts_owed() const noexcept { return start_mounts_.size(); }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    friend struct MountQueueTestAccess;
    struct SaveMount;

    static constexpr std::chrono::nanoseconds kSaveMountBound = cores::kDiscMountBoundNs;

    void arm_ladder_(Host& host, const MountAsk& ask, const cores::CoreFactory& row);
    void arm_plain_mount_(Host& host, const MountAsk& ask);
    void arm_start_mount_(Host& host, const MountAsk& ask);

    [[nodiscard]] std::optional<proto::FioIndexByte> pick_index_word_(
        proto::IoIndex index,
        std::string_view path
    ) const;
    void arm_boot_image_(const MountAsk& ask);
    [[nodiscard]] bool start_mounts_live_() const noexcept;
    void finish_save_mount_(Host& host, bool ok, bool quiet = false);

    [[nodiscard]] std::optional<svc::MountRefusal> refusal_at_(proto::SlotIndex slot) noexcept;

    [[nodiscard]] bool on_known_(Host& host, proto::SlotIndex slot);
    void on_timeout_(Host& host, proto::SlotIndex slot);
    void advance_unmount_(Host& host, proto::SlotIndex slot);
    void advance_detach_(Host& host, proto::SlotIndex slot);
    void advance_attach_(Host& host, proto::SlotIndex slot);

    void begin_mirror_(SaveMount& m);

    void advance_mirror_();

    [[nodiscard]] bool hand_load_(Host& host);
    [[nodiscard]] bool announce_(Host& host, proto::SlotIndex slot);

    [[nodiscard]] bool order_class_mount_(Host& host, const MountAsk& ask, proto::FileSize bytes);

    [[nodiscard]] bool order_class_eject_(Host& host, proto::IoIndex index);

    [[nodiscard]] bool mount_busy_() const noexcept;
    void publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept;
    void publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept;

    const svc::Vfs& vfs_;
    svc::CardWriter card_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    EventQueue<SeatTag::Main>::Producer owner_events_;
    const svc::MountRefusalCell& refusals_;
    svc::MountRefusalCell::Reader refusal_reader_{};
    const os::IClock& clock_;
    OwnerLadder& ladder_;
    const NegotiatedCore& negotiated_;
    OwnerTally& owner_tally_;
    ILoadWindowMap<seat_owner::Main>& window_map_;
    hal::FpgaAperture aperture_{};
    Tally tally_{};

    bool start_pick_ladder_ = false;

    std::optional<MountAsk> held_mount_;

    struct SaveMount {

        enum class Step : std::uint8_t {
            Unmount,
            Act,
            Mirror,
            Announce,
            ClassHalf,
        };

        [[nodiscard]] static Step first_step(const MountAsk& ask) noexcept {
            return ask.path.view().empty() ? Step::Unmount : Step::Act;
        }

        MountAsk ask{};
        Step step = first_step(ask);
        proto::ActKey gen{};
        proto::FileSize bytes{};
        os::Deadline due{os::Deadline::immediate()};

        bool bracketed = false;

        std::optional<ContentRequest> then_load{};
        bool start = false;

        bool class_follows = false;

        std::optional<MirrorFill> fill{};
    };

    std::optional<SaveMount> save_mount_;

    infra::IndexedArray<PathText, proto::kAnnounceableSlots, proto::SlotIndex> mounted_{};

    proto::BlockSlotMask announced_{};

    std::deque<MountAsk> start_mounts_{};

    std::deque<MountAsk> boot_images_{};
};

}
