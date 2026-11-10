// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_option_cells.h"

#include "app/interned_file.h"

namespace mister::app {

void CoreOptionCells::answer_no_bytes_(
    Host& host,
    proto::SaveKind kind,
    proto::SaveStatus status,
    proto::CorrelationTag tag
) noexcept {
    (void)host.options_push_save_bytes(SaveAnswer{.which = kind, .status = status, .tag = tag});
}

void CoreOptionCells::answer_save_ask_(
    const proto::LinkOp::SaveAsk& ask,
    infra::OptRef<cores::Core> core,
    Host& host
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    const proto::SaveKind kind = ask.which;
    const CorrelationTag tag = ask.tag;

    infra::OptRef<cores::IDipSwitches> sw{};
    infra::OptRef<cores::IConfigSlots> cs{};
    FixedStr<kConfigRelCap, StrFit::Reject> rel;
    std::span<const std::byte> payload;

    std::array<std::uint8_t, 8> dips{};
    std::byte pack[kSlotPackBytes]{};

    switch (kind) {
        case proto::SaveKind::Dips: {
            if (core.has_value())
                sw = core->dip_switches();
            if (!sw)
                return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);
            if (!sw->dips_dirty())
                return answer_no_bytes_(host, kind, proto::SaveStatus::Nothing, tag);
            rel = config_rel("config/dips/", sw->dip_file_name());
            dips = sw->dip_bytes();
            payload = std::as_bytes(std::span<const std::uint8_t>(dips));
            break;
        }
        case proto::SaveKind::Slot: {
            if (core.has_value())
                cs = core->config_slots();
            if (!cs)
                return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);

            payload = cs->value(pack);
            if (payload.empty())
                return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);
            break;
        }
        case proto::SaveKind::Nvram:
            return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);
    }

    if (kind == proto::SaveKind::Dips && rel.empty()) {
        return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);
    }
    SaveAnswer answer{.which = kind, .status = proto::SaveStatus::Bytes, .tag = tag};
    if (!rel.empty()) {
        const auto path_id = host.options_intern_save_bytes(
            infra::text_bytes<const std::uint8_t>(rel.view())
        );
        if (!path_id)
            return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);
        answer.rel_path = *path_id;
    }
    if (!payload.empty()) {
        const auto id = host.options_intern_save_bytes(
            infra::byte_cast<const std::uint8_t>(payload)
        );
        if (!id)
            return answer_no_bytes_(host, kind, proto::SaveStatus::Refused, tag);
        answer.payload = *id;
    }
    const bool accepted = host.options_push_save_bytes(answer);
    if (accepted && kind == proto::SaveKind::Dips && sw.has_value()) {

        sw->mark_dips_saved();
        ++tally_.dip_saves;
    }
}

ILinkEncoder::Outcome CoreOptionCells::on(
    const proto::LinkOp::BindSlotConfig& a,
    const LinkOpCtx& ctx,
    Host&
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    infra::OptRef<cores::IConfigSlots> cs{};
    if (ctx.core.has_value())
        cs = ctx.core->config_slots();
    if (!cs) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::span<const std::byte> value{};
    if (a.file != proto::FileId{}) {
        const infra::OptRef<const InternedFile> file = ctx.tx.file(a.file);
        if (!file) {
            return ILinkEncoder::Outcome::Dropped;
        }
        value = std::as_bytes(std::span<const std::uint8_t>(file->bytes));
    }
    if (auto r = cs->apply(cores::ConfigBlob{value}); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    ++tally_.config_loads;
    publish_option_table(ctx.core);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome CoreOptionCells::on(
    const proto::LinkOp::SaveAsk& a,
    const LinkOpCtx& ctx,
    Host& host
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    answer_save_ask_(a, ctx.core, host);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome CoreOptionCells::misrouted(
    const proto::LinkOp&,
    const LinkOpCtx&,
    Host&
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    ++tally_.link_op_misrouted;
    return ILinkEncoder::Outcome::Dropped;
}

void CoreOptionCells::publish_option_table(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    option_scratch_ = OptionTable{};
    if (core.has_value()) {

        const cores::CoreProfile& p = core->profile();
        const auto rows = core->options();
        if (!rows) {
            option_cell_.publish(option_scratch_);
            return;
        }
        const auto decls = rows->option_rows();
        const std::size_t cap = std::size(option_scratch_.rows);
        const std::size_t n = decls.size() < cap ? decls.size() : cap;
        option_scratch_.count = static_cast<std::uint8_t>(n);
        for (std::size_t i = 0; i < n; ++i) {
            const cores::OptionRowView v = rows->option_view(
                proto::OptionRowIndex{static_cast<std::uint8_t>(i)}
            );
            OptionTable::Row& row = option_scratch_.rows[i];
            row.decl = infra::OptRef<const cores::OptionRow>{decls[i]};
            row.current = v.current;
            row.dimmed = v.dimmed;
            (void)row.text.assign(v.text);

            const cores::OptionRow::FileArm* file = decls[i].file_arm();
            if (file != nullptr) {
                for (const cores::FileSlot& fs : p.slots) {
                    if (fs.index == file->slot) {
                        (void)row.ext.assign(fs.extensions);
                        break;
                    }
                }
            }
        }
        proto::for_each_page(p.option_pages, [&](proto::SubmenuNo no, std::string_view page) {
            const auto title = option_scratch_.page_title.find(no);
            if (title)
                (void)title->assign(page);
        });
    }
    option_cell_.publish(option_scratch_);
}

void CoreOptionCells::publish_dip_labels(
    infra::OptRef<cores::Core> core,
    proto::BindGeneration gen
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    dip_bind_gen_ = gen;
    dip_labels_scratch_ = DipLabels{};
    dip_labels_scratch_.gen = gen;
    infra::OptRef<cores::IDipSwitches> sw{};
    if (core.has_value())
        sw = core->dip_switches();
    if (sw) {
        const std::size_t cap = std::size(dip_labels_scratch_.rows);
        const std::size_t n = sw->dip_count() < cap ? sw->dip_count() : cap;
        dip_labels_scratch_.count = proto::DipRowCount::of(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto at = proto::DipRowIndex::of(i);
            const cores::DipRowView rv = sw->dip_row(at);
            DipLabels::Row& row = at.pick(dip_labels_scratch_.rows);
            (void)row.name.assign(rv.name);
            row.choices = rv.choices.capped_to(row.ids);
            row.choices.each([&](proto::DipChoice m) {
                (void)m.pick(row.ids).assign(sw->dip_choice(at, m));
            });
        }
    }
    dip_labels_cell_.publish(dip_labels_scratch_);
}

void CoreOptionCells::publish_dip_choices(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    dip_choices_scratch_ = DipChoices{};
    dip_choices_scratch_.gen = dip_bind_gen_;
    infra::OptRef<cores::IDipSwitches> sw{};
    if (core.has_value())
        sw = core->dip_switches();
    if (sw) {
        const std::size_t cap = std::size(dip_choices_scratch_.current);
        const std::size_t n = sw->dip_count() < cap ? sw->dip_count() : cap;
        for (std::size_t i = 0; i < n; ++i) {
            const auto at = proto::DipRowIndex::of(i);
            const cores::DipRowView rv = sw->dip_row(at);
            at.pick(dip_choices_scratch_.current) = rv.current.within(rv.choices)
                ? rv.current
                : proto::DipChoice{};
        }
    }
    dip_choices_cell_.publish(dip_choices_scratch_);
}

Ex<void> CoreOptionCells::set_dip_counted(
    infra::OptRef<cores::Core> core,
    proto::DipRowIndex row,
    proto::DipChoice choice
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    infra::OptRef<cores::IDipSwitches> sw{};
    if (core.has_value())
        sw = core->dip_switches();
    if (!sw)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (auto r = sw->set_dip(row, choice); !r)
        return r;
    ++tally_.dip_sets;
    publish_dip_choices(core);
    return {};
}

Ex<void> CoreOptionCells::set_option_counted(
    infra::OptRef<cores::Core> core,
    proto::OptionRowIndex row,
    proto::FieldValue choice
) noexcept {
    TASTY_SEAT_BODY(CoreOptionCells);
    infra::OptRef<cores::IOptionRows> rows{};
    if (core.has_value())
        rows = core->options();
    if (!rows)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (auto r = rows->set_option(row, choice); !r)
        return r;
    ++tally_.option_sets;
    publish_option_table(core);
    return {};
}

}
