// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/conf_str_cell.h"
#include "app/event.h"
#include "app/mgl.h"
#include "app/path_text.h"
#include "app/types.h"
#include "infra/diag_log.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::app {

class InfoSink;
class IdentityLatch;

inline constexpr std::size_t kMglDocMax = 64u * 1024u;

struct MglPumpStats {
    std::uint32_t routed = 0;
    std::uint32_t superseded = 0;
    std::uint32_t read_failures = 0;
    std::uint32_t no_rbf = 0;
    std::uint32_t loads = 0;
    std::uint32_t load_retries = 0;
    std::uint32_t armed = 0;
    std::uint32_t abandoned = 0;
    std::uint32_t arm_timeouts = 0;

    std::uint32_t file_items = 0;
    std::uint32_t publishes = 0;

    std::uint32_t item_drops = 0;

    std::uint32_t homes_unbound = 0;

    std::uint32_t busy_replays = 0;
};

using MglPumpCell = xthread::Telemetry<MglPumpStats, SeatTag::Ui>;

class MglPump {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class State : std::uint8_t { Idle, PendingRead, AwaitingCore, Playing };

    MglPump(const svc::Vfs& vfs, const os::IClock& clock) noexcept : vfs_(&vfs), clock_(&clock) {}

    MglPump(const MglPump&) = delete;
    MglPump& operator=(const MglPump&) = delete;

    void set_info_sink(InfoSink* sink) noexcept { info_ = sink; }

    void set_link_tx(LinkTxChannel* tx) noexcept {
        link_tx_ = tx;
        player_.set_link_tx(tx);
    }

    void set_asks(UiRequestRing* asks) noexcept {
        asks_ = asks;
        player_.set_asks(asks);
    }

    void set_diag(xthread::DiagLog* d) noexcept { diag_ = d; }

    void set_identity(const IdentityLatch* id) noexcept { identity_ = id; }

    void set_conf_cell(const ConfStrCell* cell) noexcept { conf_cell_ = cell; }

    [[nodiscard]] bool take_playlist(std::string_view path) noexcept;

    void tick();

    void on_core_loaded(CorrelationTag tag, bool mgl_capable);
    void on_session_over(CorrelationTag tag);
    void on_request_refused(CorrelationTag tag, Errc why);

    using Stats = MglPumpStats;

    [[nodiscard]] Stats stats() const noexcept { return cell_.sample().value; }

    [[nodiscard]] const MglPumpCell& stats_cell() const noexcept { return cell_; }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] InfoId last_info() const noexcept { return last_info_; }
    [[nodiscard]] std::optional<Errc> last_error() const noexcept { return last_error_; }
    [[nodiscard]] const MglPlayer& player() const noexcept { return player_; }

    [[nodiscard]] CorrelationTag load_tag() const noexcept { return load_tag_; }

    [[nodiscard]] std::string_view resolved_rbf() const noexcept {
        return std::string_view{rbf_, rbf_len_};
    }

    static constexpr unsigned kArmTimeoutMs = 10000;
    static constexpr unsigned kLoadRetryMs = 2000;

    static constexpr unsigned kMaxLoadRetries = 1;

    static constexpr unsigned kAdvanceCap = 4;

    static constexpr unsigned kBusyBoundMs = 60000;

    [[nodiscard]] std::string_view rbf_request() const noexcept {
        return std::string_view{want_, want_len_};
    }

private:
    void enter(State s) noexcept;
    void check_invariant() const noexcept;

    void begin_read();
    void pump_advance();
    void abandon(InfoId id, Errc code);

    bool publish_load();

    [[nodiscard]] Ex<void> resolve_rbf(std::string_view want);

    void bind_homes();

    void replay_busy_(CorrelationTag tag);

    [[nodiscard]] CoreScope sample_scope();

    const svc::Vfs* vfs_;
    const IdentityLatch* identity_ = nullptr;
    const ConfStrCell* conf_cell_ = nullptr;

    ConfStrText conf_scratch_{};
    const os::IClock* clock_;
    InfoSink* info_ = nullptr;
    LinkTxChannel* link_tx_ = nullptr;
    UiRequestRing* asks_ = nullptr;
    xthread::DiagLog* diag_ = nullptr;

    MglPlayer player_{};
    State state_ = State::Idle;
    Stats stats_{};

    MglPumpCell cell_{};
    struct PublishOnExit {
        MglPump* self;
        ~PublishOnExit() { self->cell_.publish(self->stats_); }
    };
    InfoId last_info_ = InfoId::None;

    std::optional<Errc> last_error_{};

    bool pending_ = false;
    std::uint16_t latch_len_ = 0;
    char latch_[kPathMax]{};

    std::uint16_t want_len_ = 0;
    char want_[kPathMax]{};
    std::uint16_t rbf_len_ = 0;
    char rbf_[kPathMax]{};

    std::uint32_t doc_len_ = 0;
    char doc_[kMglDocMax]{};

    CorrelationTag load_tag_{};

    std::uint32_t retries_used_ = 0;

    os::Deadline arm_deadline_ = os::Deadline::immediate();
    os::Deadline retry_deadline_ = os::Deadline::immediate();

    std::optional<os::Deadline> busy_since_{};
};

}  // namespace mister::app
