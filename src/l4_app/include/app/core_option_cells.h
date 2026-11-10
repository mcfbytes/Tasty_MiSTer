// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "app/config_rel.h"
#include "app/core_option_acts.h"
#include "app/dip_choices.h"
#include "app/dip_labels.h"
#include "app/link_encoder.h"
#include "app/link_op_ctx.h"
#include "app/link_op_dispatch.h"
#include "app/link_tx_channel.h"
#include "app/types.h"
#include "app/option_cell.h"
#include "cores/config_codec.h"
#include "cores/config_slots.h"
#include "cores/core_support.h"
#include "cores/dip_switches.h"
#include "cores/option_rows.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/link_op.h"
#include "proto/save_kind.h"
#include "proto/types.h"

namespace mister::app {

class CoreOptionCells final : public ICoreOptionActs {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {
        std::uint32_t dip_sets = 0;
        std::uint32_t dip_saves = 0;
        std::uint32_t config_loads = 0;
        std::uint32_t option_sets = 0;
        std::uint32_t link_op_misrouted = 0;
    };

    struct SaveAnswer {
        proto::SaveKind which{};
        proto::SaveStatus status{};
        proto::RxSlabId rel_path{};
        proto::RxSlabId payload{};
        proto::CorrelationTag tag{};
    };

    static_assert(
        sizeof(SaveAnswer) == sizeof(proto::LinkEvent::SaveBytes),
        "a SaveAnswer's extent is the SaveBytes record's"
    );
    static_assert(offsetof(SaveAnswer, which) == offsetof(proto::LinkEvent::SaveBytes, which));
    static_assert(offsetof(SaveAnswer, status) == offsetof(proto::LinkEvent::SaveBytes, status));
    static_assert(
        offsetof(SaveAnswer, rel_path) == offsetof(proto::LinkEvent::SaveBytes, rel_path)
    );
    static_assert(offsetof(SaveAnswer, payload) == offsetof(proto::LinkEvent::SaveBytes, payload));
    static_assert(offsetof(SaveAnswer, tag) == offsetof(proto::LinkEvent::SaveBytes, tag));

    class Host {
    public:
        virtual ~Host() = default;

        [[nodiscard]] virtual bool options_push_save_bytes(const SaveAnswer& a) noexcept = 0;
        [[nodiscard]] virtual Ex<proto::RxSlabId> options_intern_save_bytes(
            std::span<const std::uint8_t> bytes
        ) noexcept = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
    };

    CoreOptionCells() noexcept = default;

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::BindSlotConfig& a, const LinkOpCtx& ctx, Host& host) noexcept;
    Result on(const proto::LinkOp::SaveAsk& a, const LinkOpCtx& ctx, Host& host) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx, Host& host) noexcept;

    [[nodiscard]] Ex<void> set_dip_counted(
        infra::OptRef<cores::Core> core,
        proto::DipRowIndex row,
        proto::DipChoice choice
    ) noexcept override;
    [[nodiscard]] Ex<void> set_option_counted(
        infra::OptRef<cores::Core> core,
        proto::OptionRowIndex row,
        proto::FieldValue choice
    ) noexcept override;

    void publish_dip_labels(infra::OptRef<cores::Core> core, proto::BindGeneration gen) noexcept;
    void publish_dip_choices(infra::OptRef<cores::Core> core) noexcept;
    void publish_option_table(infra::OptRef<cores::Core> core) noexcept;

    [[nodiscard]] const DipLabelsCell& dip_labels_cell() const noexcept { return dip_labels_cell_; }

    [[nodiscard]] const DipChoicesCell& dip_choices_cell() const noexcept {
        return dip_choices_cell_;
    }

    [[nodiscard]] const OptionCell& option_cell() const noexcept { return option_cell_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    static constexpr std::size_t kSlotPackBytes = 8192;
    static_assert(
        cores::kMaxConfigValueBytes <= kSlotPackBytes,
        "the save-ask pack holds any core's decoded value"
    );

    void answer_save_ask_(
        const proto::LinkOp::SaveAsk& ask,
        infra::OptRef<cores::Core> core,
        Host& host
    ) noexcept;

    static void answer_no_bytes_(
        Host& host,
        proto::SaveKind kind,
        proto::SaveStatus status,
        proto::CorrelationTag tag
    ) noexcept;

    proto::BindGeneration dip_bind_gen_{};
    DipLabels dip_labels_scratch_{};
    DipLabelsCell dip_labels_cell_{};
    DipChoices dip_choices_scratch_{};
    DipChoicesCell dip_choices_cell_{};
    OptionTable option_scratch_{};
    OptionCell option_cell_{};
    Tally tally_{};
};

static_assert(LinkOpOptionsSink<CoreOptionCells>);

}
