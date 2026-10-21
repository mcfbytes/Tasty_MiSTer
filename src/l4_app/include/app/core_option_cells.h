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
#include "app/config_slot_cell.h"
#include "app/core_option_acts.h"
#include "app/dip_cell.h"
#include "app/link_encoder.h"
#include "app/link_op_ctx.h"
#include "app/link_op_dispatch.h"
#include "app/link_tx_channel.h"
#include "app/types.h"
#include "app/option_cell.h"
#include "cores/config_slots.h"
#include "cores/core_support.h"
#include "cores/dip_switches.h"
#include "cores/option_rows.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

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

    class Host {
    public:
        virtual ~Host() = default;
        [[nodiscard]] virtual bool options_push_save_bytes(
            proto::SaveKind kind,
            proto::SaveStatus st,
            proto::RxSlabId path,
            proto::RxSlabId data,
            proto::CorrelationTag tag
        ) noexcept = 0;
        [[nodiscard]] virtual Ex<proto::RxSlabId> options_intern_save_bytes(
            std::span<const std::uint8_t> bytes
        ) noexcept = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
    };

    explicit CoreOptionCells(infra::OptRef<const svc::Vfs> vfs) noexcept : vfs_(vfs) {}

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::BindSlotConfig& a, const LinkOpCtx& ctx, Host& host) noexcept;
    Result on(const proto::LinkOp::BindSlotPreviews& a, const LinkOpCtx& ctx, Host& host) noexcept;
    Result on(const proto::LinkOp::SaveAsk& a, const LinkOpCtx& ctx, Host& host) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx, Host& host) noexcept;

    [[nodiscard]] bool set_dip_counted(
        infra::OptRef<cores::Core> core,
        std::uint8_t row,
        std::uint32_t choice
    ) noexcept override;
    [[nodiscard]] bool set_option_counted(
        infra::OptRef<cores::Core> core,
        std::uint8_t row,
        std::uint8_t choice
    ) noexcept override;

    void publish_dip_table(infra::OptRef<cores::Core> core) noexcept;
    void publish_option_table(infra::OptRef<cores::Core> core) noexcept;
    void publish_config_slots(infra::OptRef<cores::Core> core) noexcept;

    [[nodiscard]] const DipCell& dip_cell() const noexcept { return dip_cell_; }

    [[nodiscard]] const OptionCell& option_cell() const noexcept { return option_cell_; }

    [[nodiscard]] const ConfigSlotCell& config_slot_cell() const noexcept {
        return config_slot_cell_;
    }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    static constexpr std::size_t kSlotPackBytes = 8192;
    [[nodiscard]] static std::size_t pack_slot_names(
        cores::IConfigSlots& cs,
        std::span<std::byte> dst
    );
    template <std::size_t N>
    static void copy_cell_text(char (&dst)[N], std::string_view src) noexcept;

    void answer_save_ask_(
        const proto::LinkOp::SaveAsk& ask,
        infra::OptRef<cores::Core> core,
        Host& host
    ) noexcept;
    [[nodiscard]] static bool push_save_bytes_(
        proto::SaveKind kind,
        proto::SaveStatus st,
        proto::RxSlabId path,
        proto::RxSlabId data,
        proto::CorrelationTag tag,
        Host& host
    ) noexcept;

    infra::OptRef<const svc::Vfs> vfs_;
    DipTable dip_scratch_{};
    DipCell dip_cell_{};
    ConfigSlotTable config_slot_scratch_{};
    ConfigSlotCell config_slot_cell_{};
    OptionTable option_scratch_{};
    OptionCell option_cell_{};
    Tally tally_{};
};

inline std::size_t CoreOptionCells::pack_slot_names(
    cores::IConfigSlots& cs,
    std::span<std::byte> dst
) {
    const auto n = static_cast<std::uint8_t>(
        std::min<std::size_t>(cs.slot_count(), cores::kMaxConfigSlots)
    );
    if (dst.empty())
        return 0;
    dst[0] = std::byte{n};
    std::size_t off = 1;
    for (std::uint8_t i = 0; i < n; ++i) {
        const auto name = cs.file_name(cores::ConfigSlot{i});
        if (off + 1 + name.size() > dst.size())
            return 0;
        dst[off++] = std::byte{static_cast<std::uint8_t>(name.size())};
        if (!name.empty()) {
            std::memcpy(dst.data() + off, name.view().data(), name.size());
            off += name.size();
        }
    }
    return off;
}

template <std::size_t N>
void CoreOptionCells::copy_cell_text(char (&dst)[N], std::string_view src) noexcept {
    const std::size_t n = src.size() < N - 1 ? src.size() : N - 1;
    if (n != 0)
        std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

inline bool CoreOptionCells::push_save_bytes_(
    proto::SaveKind kind,
    proto::SaveStatus st,
    proto::RxSlabId path,
    proto::RxSlabId data,
    CorrelationTag tag,
    Host& host
) noexcept {
    return host.options_push_save_bytes(kind, st, path, data, tag);
}

inline void CoreOptionCells::answer_save_ask_(
    const proto::LinkOp::SaveAsk& ask,
    infra::OptRef<cores::Core> core,
    Host& host
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    const proto::SaveKind kind = ask.which;
    const CorrelationTag tag = ask.tag;

    cores::IDipSwitches* sw = nullptr;
    cores::IConfigSlots* cs = nullptr;
    FixedStr<kConfigRelCap, StrFit::Reject> rel;
    std::span<const std::byte> payload;

    std::array<std::uint8_t, 8> dips{};
    std::byte pack[kSlotPackBytes]{};

    switch (kind) {
        case proto::SaveKind::Dips: {
            sw = core.has_value() ? core->dip_switches() : nullptr;
            if (sw == nullptr) {
                return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag, host);
            }
            if (!sw->dips_dirty()) {
                return (void)push_save_bytes_(kind, proto::SaveStatus::Nothing, {}, {}, tag, host);
            }
            rel = config_rel("config/dips/", sw->dip_file_name());
            dips = sw->dip_bytes();
            payload = std::as_bytes(std::span<const std::uint8_t>(dips));
            break;
        }
        case proto::SaveKind::Slot:
        case proto::SaveKind::SlotNames: {
            cs = core.has_value() ? core->config_slots() : nullptr;
            if (cs == nullptr) {

                if (!core.has_value())
                    publish_config_slots(core);
                return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag, host);
            }
            const auto fname = cs->file_name(cores::ConfigSlot{ask.slot});
            rel = config_rel("config/", fname.view());

            payload = kind == proto::SaveKind::Slot
                ? cs->snapshot()
                : std::span<const std::byte>(pack, pack_slot_names(*cs, pack));
            break;
        }
        case proto::SaveKind::Nvram:
            return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag, host);
    }

    if (rel.empty()) {
        return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag, host);
    }
    const auto path_id = host.options_intern_save_bytes(
        infra::text_bytes<const std::uint8_t>(rel.view())
    );
    if (!path_id) {
        return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag, host);
    }
    proto::RxSlabId data_id{};
    if (!payload.empty()) {
        const auto id = host.options_intern_save_bytes(
            infra::byte_cast<const std::uint8_t>(payload)
        );
        if (!id) {
            return (void)push_save_bytes_(kind, proto::SaveStatus::Refused, {}, {}, tag, host);
        }
        data_id = *id;
    }
    const bool
        accepted = push_save_bytes_(kind, proto::SaveStatus::Bytes, *path_id, data_id, tag, host);
    if (accepted && kind == proto::SaveKind::Dips && sw != nullptr) {

        sw->mark_dips_saved();
        ++tally_.dip_saves;
    }
}

inline ILinkEncoder::Outcome CoreOptionCells::on(
    const proto::LinkOp::BindSlotConfig& a,
    const LinkOpCtx& ctx,
    Host&
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    cores::IConfigSlots* cs = ctx.core.has_value() ? ctx.core->config_slots() : nullptr;
    if (cs == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::span<const std::byte> blob{};
    if (a.file != proto::FileId{}) {
        const InternedFile* const file = ctx.inbox.file(a.file);
        if (file == nullptr) {
            return ILinkEncoder::Outcome::Dropped;
        }
        blob = std::as_bytes(std::span<const std::uint8_t>(file->bytes));
    }
    if (auto r = cs->restore(cores::ConfigBlob{blob}); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    ++tally_.config_loads;
    publish_option_table(ctx.core);
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome CoreOptionCells::on(
    const proto::LinkOp::BindSlotPreviews& a,
    const LinkOpCtx& ctx,
    Host&
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    cores::IConfigSlots* cs = ctx.core.has_value() ? ctx.core->config_slots() : nullptr;
    if (cs == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto count = std::min<std::uint8_t>(a.count, cores::kMaxConfigSlots);
    config_slot_scratch_ = ConfigSlotTable{};
    config_slot_scratch_.count = count;
    for (std::uint8_t i = 0; i < count; ++i) {
        const InternedFile* const file = ctx.inbox.file(InternedFile::config_id(i));
        std::span<const std::byte> blob{};
        if (file != nullptr && !file->bytes.empty()) {
            blob = std::as_bytes(std::span<const std::uint8_t>(file->bytes));
            config_slot_scratch_.exists |= static_cast<std::uint16_t>(1u << i);
        }
        config_slot_scratch_.preview[i] = cs->preview(cores::ConfigBlob{blob});
    }
    config_slot_cell_.publish(config_slot_scratch_);
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome CoreOptionCells::on(
    const proto::LinkOp::SaveAsk& a,
    const LinkOpCtx& ctx,
    Host& host
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    answer_save_ask_(a, ctx.core, host);
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome CoreOptionCells::misrouted(
    const proto::LinkOp&,
    const LinkOpCtx&,
    Host&
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    ++tally_.link_op_misrouted;
    return ILinkEncoder::Outcome::Dropped;
}

inline void CoreOptionCells::publish_config_slots(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    cores::IConfigSlots* cs = core.has_value() ? core->config_slots() : nullptr;
    if (cs == nullptr || !vfs_)
        config_slot_scratch_ = ConfigSlotTable{};
    config_slot_cell_.publish(config_slot_scratch_);
}

inline void CoreOptionCells::publish_option_table(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    cores::IOptionRows* rows = core.has_value() ? core->options() : nullptr;
    option_scratch_ = OptionTable{};
    if (rows != nullptr) {
        const cores::CoreProfile& p = core->profile();
        const auto decls = rows->option_rows();
        const std::size_t cap = std::size(option_scratch_.rows);
        const std::size_t n = decls.size() < cap ? decls.size() : cap;
        option_scratch_.count = static_cast<std::uint8_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const cores::OptionRowView v = rows->option_view(i);
            OptionTable::Row& row = option_scratch_.rows[i];
            row.decl = infra::OptRef<const cores::OptionRow>{decls[i]};
            row.current = v.current;
            row.dimmed = v.dimmed;
            (void)row.text.assign(v.text);

            if (decls[i].kind == cores::OptionRow::Kind::File) {
                for (const cores::FileSlot& fs : p.slots) {
                    if (fs.index == decls[i].slot) {
                        (void)row.ext.assign(fs.extensions);
                        break;
                    }
                }
            }
        }
        const std::size_t pages = std::size(option_scratch_.page_title);
        for (std::size_t i = 0; i < p.option_pages.size() && i < pages; ++i) {
            (void)option_scratch_.page_title[i].assign(p.option_pages[i]);
        }
    }
    option_cell_.publish(option_scratch_);
}

inline void CoreOptionCells::publish_dip_table(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    dip_scratch_ = DipTable{};
    cores::IDipSwitches* sw = core.has_value() ? core->dip_switches() : nullptr;
    if (sw != nullptr) {
        const std::size_t cap = std::size(dip_scratch_.rows);
        const std::size_t n = sw->dip_count() < cap ? sw->dip_count() : cap;
        dip_scratch_.count = static_cast<std::uint8_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const cores::DipRowView rv = sw->dip_row(i);
            DipTable::Row& row = dip_scratch_.rows[i];
            copy_cell_text(row.name, rv.name);
            const std::size_t ids = std::size(row.ids);
            row.choices = static_cast<std::uint8_t>(rv.choices < ids ? rv.choices : ids);
            row.current = rv.current < row.choices ? rv.current : 0;
            for (std::size_t m = 0; m < row.choices; ++m) {
                copy_cell_text(row.ids[m], sw->dip_choice(i, m));
            }
        }
    }
    dip_cell_.publish(dip_scratch_);
}

inline bool CoreOptionCells::set_dip_counted(
    infra::OptRef<cores::Core> core,
    std::uint8_t row,
    std::uint32_t choice
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    cores::IDipSwitches* sw = core.has_value() ? core->dip_switches() : nullptr;
    if (sw == nullptr)
        return false;
    if (auto r = sw->set_dip(row, choice); !r)
        return false;
    ++tally_.dip_sets;
    publish_dip_table(core);
    return true;
}

inline bool CoreOptionCells::set_option_counted(
    infra::OptRef<cores::Core> core,
    std::uint8_t row,
    std::uint8_t choice
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    cores::IOptionRows* rows = core.has_value() ? core->options() : nullptr;
    if (rows == nullptr)
        return false;
    if (auto r = rows->set_option(row, choice); !r)
        return false;
    ++tally_.option_sets;
    publish_option_table(core);
    return true;
}

static_assert(LinkOpOptionsSink<CoreOptionCells>);

}
