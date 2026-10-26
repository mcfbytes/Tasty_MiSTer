// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/block_poll_route.h"
#include "app/sys_grant.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/rt_stats.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/axi_status_decoder.h"
#include "proto/decoder_row.h"
#include "proto/spi_block_decoder.h"
#include "proto/spi_conf_str_decoder.h"
#include "proto/spi_identify_decoder.h"
#include "proto/spi_status_decoder.h"
#include "proto/status_cell.h"
#include "proto/status_snapshot.h"
#include "proto/types.h"
#include "proto/user_io/poll_params.h"
#include "proto/user_io/poll_reader.h"
#include "reactor/link_decoder.h"

namespace mister::cores {
class Core;
}

namespace mister::hal {
class ICoreSignals;
class ISpiTransport;
}

namespace mister::proto {
class BlockSlots;
class CoreSession;
class ILinkRouter;
class ISessionLive;
}

namespace mister::app {

class InputWire;
class LinkBinder;
class OsdWire;
class SessionBindings;

class SessionRows final : private proto::user_io::IPollParams {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {

        std::uint32_t census_row_drops = 0;
    };

    struct Wiring {
        hal::ISpiTransport& link;
        hal::ICoreSignals& signals;
        proto::ILinkRouter& router;
        const proto::ISessionLive& live;
        proto::CoreSession& session;
        SessionBindings& bindings;
        LinkBinder& binder;
        proto::BlockSlots& slots;
        OsdWire& osd;
        InputWire& input;

        infra::OptRef<const proto::StatusSnapshotCell> status_lw{};
    };

    explicit SessionRows(const Wiring& w) noexcept;
    SessionRows(const SessionRows&) = delete;
    SessionRows& operator=(const SessionRows&) = delete;

    [[nodiscard]] Ex<void> rebind_census(
        std::span<const reactor::LinkDecoderDecl> rows,
        cores::Core* owner
    );

    [[nodiscard]] Ex<void> rebind_pre_session(proto::BindGeneration gen, cores::Core* owner);

    void bind_status_route(StatusRoute route, std::uint16_t gen) noexcept;

    [[nodiscard]] std::span<const reactor::LinkDecoderDecl> census_rows(
        std::array<reactor::LinkDecoderDecl, kMaxServices>& out,
        infra::OptRef<cores::Core> current
    ) noexcept;

    [[nodiscard]] std::optional<bool> latch_osd_edge();

    void publish_status_word() noexcept;

    void disarm_polls() noexcept;

    void arm_session_polls(bool front_end) noexcept;

    void bind_block_poll(BlockPollRoute route) noexcept { block_poll_ = route; }

    void bind_block_drain(infra::OptRef<cores::Core> current) noexcept;

    [[nodiscard]] proto::user_io::IPollParams& poll_params() noexcept { return *this; }

    [[nodiscard]] const proto::StatusCell& status_cell() const noexcept { return status_cell_; }

    [[nodiscard]] const proto::SpiStatusDecoder& status_decoder() const noexcept {
        return status_decoder_;
    }

    [[nodiscard]] const proto::AxiStatusDecoder& axi_status_decoder() const noexcept {
        return axi_status_decoder_;
    }

    [[nodiscard]] StatusRoute status_route() const noexcept { return status_route_; }

    [[nodiscard]] const proto::SpiConfStrDecoder& conf_str_decoder() const noexcept {
        return conf_str_decoder_;
    }

    [[nodiscard]] const proto::SpiBlockDecoder& block_decoder() const noexcept {
        return block_decoder_;
    }

    [[nodiscard]] const proto::user_io::PollReader& info_poll() const noexcept {
        return info_poll_;
    }

    [[nodiscard]] const proto::user_io::PollReader& rumble_poll() const noexcept {
        return rumble_poll_;
    }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    [[nodiscard]] std::uint8_t issued_params(proto::user_io::PollShape s) noexcept override;

    [[nodiscard]] std::size_t put_census_row_(
        std::span<reactor::LinkDecoderDecl> out,
        std::size_t n,
        const reactor::LinkDecoderDecl& row
    ) noexcept;
    [[nodiscard]] std::string_view conf_str_name() const noexcept;

    proto::CoreSession& session_;
    SessionBindings& bindings_;
    LinkBinder& binder_;
    OsdWire& osd_;
    InputWire& input_;

    proto::StatusWord status_scratch_{};
    proto::StatusCell status_cell_{};

    proto::SpiIdentifyDecoder identify_decoder_;
    proto::DecoderRow<proto::SpiIdentifyDecoder> identify_row_{identify_decoder_};
    proto::SpiStatusDecoder status_decoder_;
    proto::DecoderRow<proto::SpiStatusDecoder> status_row_{status_decoder_};
    proto::AxiStatusDecoder axi_status_decoder_;
    proto::DecoderRow<proto::AxiStatusDecoder> axi_status_row_{axi_status_decoder_};
    infra::OptRef<const proto::StatusSnapshotCell> status_lw_{};
    StatusRoute status_route_ = StatusRoute::Link;
    proto::SpiConfStrDecoder conf_str_decoder_;
    proto::DecoderRow<proto::SpiConfStrDecoder> conf_str_row_{conf_str_decoder_};
    proto::SpiBlockDecoder block_decoder_;
    proto::DecoderRow<proto::SpiBlockDecoder> block_row_{block_decoder_};
    proto::user_io::PollReader info_poll_;
    proto::DecoderRow<proto::user_io::PollReader> info_row_{info_poll_};
    proto::user_io::PollReader rumble_poll_;

    xthread::Telemetry<std::uint8_t, SeatTag::Input>::Reader rumble_pads_reader_{};
    std::uint8_t rumble_pads_ = 0;
    proto::DecoderRow<proto::user_io::PollReader> rumble_row_{rumble_poll_};
    BlockPollRoute block_poll_ = BlockPollRoute::Link;

    bool osd_visible_last_ = false;
    Tally tally_{};
};

}
