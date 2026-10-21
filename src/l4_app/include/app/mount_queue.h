// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>

#include "app/content_request.h"
#include "app/event.h"
#include "app/link_tx_channel.h"
#include "app/owner_tally.h"
#include "app/path_text.h"
#include "app/start_chain.h"
#include "app/ui_request.h"
#include "cores/ladder_host.h"
#include "cores/registry.h"
#include "cores/staging_core.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/link_op.h"

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
        std::optional<std::uint8_t> index_word{};
    };

    class Host : public virtual cores::ILadderHost {
    public:
        [[nodiscard]] virtual bool arm_loader(
            const ContentRequest& req,
            std::string_view save,
            std::uint64_t save_bytes
        ) = 0;
        virtual void perform_content(const ContentRequest& req) = 0;
        virtual void launch_content(const ContentRequest& cr) = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        ~Host() override = default;
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
    };

    struct Wiring {
        const svc::Vfs& vfs;
        LinkTxChannel<SeatTag::Main>& inbox;
        EventQueue<SeatTag::Main>& events;
        const os::IClock& clock;
        OwnerLadder& ladder;
        const NegotiatedCore& negotiated;
        OwnerTally& owner_tally;
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

    [[nodiscard]] bool save_holds_load() const noexcept {
        return save_mount_ && save_mount_->then_load;
    }

    [[nodiscard]] bool mount_held() const noexcept { return held_mount_.has_value(); }

    [[nodiscard]] std::size_t start_mounts_owed() const noexcept { return start_mounts_.size(); }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    friend struct MountQueueTestAccess;

    static constexpr std::int64_t kSaveMountBoundNs = cores::kDiscMountBoundNs;

    void arm_ladder_(Host& host, const MountAsk& ask, const cores::CoreFactory& row);
    void arm_plain_mount_(Host& host, const MountAsk& ask);
    void arm_start_mount_(Host& host, const MountAsk& ask);

    [[nodiscard]] std::optional<std::uint8_t> pick_index_word_(
        proto::IoIndex index,
        std::string_view path
    ) const;
    void arm_boot_image_(const MountAsk& ask);
    [[nodiscard]] bool start_mounts_live_() const noexcept;
    void finish_save_mount_(Host& host, bool ok, bool quiet = false);

    [[nodiscard]] bool mount_busy_() const noexcept;
    void publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept;
    void publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept;

    const svc::Vfs& vfs_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    EventQueue<SeatTag::Main>& owner_events_;
    const os::IClock& clock_;
    OwnerLadder& ladder_;
    const NegotiatedCore& negotiated_;
    OwnerTally& owner_tally_;
    Tally tally_{};

    bool start_pick_ladder_ = false;

    std::optional<MountAsk> held_mount_;

    struct SaveMount {
        MountAsk ask{};
        std::uint32_t gen = 0;
        std::int64_t due_ns = 0;
        bool unmounted = false;

        bool bracketed = false;

        std::optional<ContentRequest> then_load{};
        bool start = false;
    };

    std::optional<SaveMount> save_mount_;

    std::deque<MountAsk> start_mounts_{};

    std::deque<MountAsk> boot_images_{};
};

}
