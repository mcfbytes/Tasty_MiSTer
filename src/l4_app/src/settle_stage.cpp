// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/settle_stage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/link_tx_channel.h"
#include "app/path_text.h"
#include "infra/diag_log.h"
#include "proto/conf_str.h"
#include "proto/item_table.h"
#include "proto/link_op.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

using Refusal = PlayRefusal::Why;

std::string value_label(
    infra::OptRef<const proto::ItemTable> table,
    const cores::IReplayCore::SettingNeed& need,
    unsigned value
) {
    constexpr unsigned kFirstLabelSubfield = 2;
    if (table) {
        for (const proto::Item& it : table->items()) {
            if (it.kind != proto::ItemKind::Option || it.bits != need.field)
                continue;
            const std::string_view label = table->subfield(it, kFirstLabelSubfield + value);
            if (!label.empty())
                return std::string(label);
            break;
        }
    }
    return std::to_string(value);
}

cores::IReplayCore::SettingNeeds where_the_core_has(
    cores::IReplayCore::SettingNeeds needs,
    infra::OptRef<const proto::ItemTable> table
) {
    constexpr unsigned kFirstLabelSubfield = 2;
    constexpr unsigned kMaxOptionBits = 8;
    if (!table)
        return needs;
    for (const std::size_t i : needs.n.indices()) {
        cores::IReplayCore::SettingNeed& n = needs.rows[i];
        const proto::Item* at = nullptr;
        for (const proto::Item& it : table->items()) {
            if (it.kind != proto::ItemKind::Option
                || !it.bits.valid()
                || table->subfield(it, 1) != n.name)
                continue;

            if (at == nullptr || (at->cond_count != 0 && it.cond_count == 0))
                at = &it;
        }
        if (at == nullptr)
            continue;

        if (at->bits.width > kMaxOptionBits
            || proto::printed_field(n.preferred) >= (1u << at->bits.width)
            || proto::printed_field(n.preferred) + kFirstLabelSubfield >= at->subfield_count) {
            n.allowed = {};
            continue;
        }
        n.field = at->bits;
    }
    return needs;
}

}

IReplayStage::Stage SettleStage::level() const noexcept {
    TASTY_SEAT_BODY(SettleStage);
    return Stage::Settling;
}

IReplayStage::Step SettleStage::stop(Ctx) noexcept {
    TASTY_SEAT_BODY(SettleStage);
    return Step::done();
}

IReplayStage::Step SettleStage::enter(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(SettleStage);
    return settle_(ctx);
}

IReplayStage::Step SettleStage::tick(Ctx ctx) noexcept {
    TASTY_SEAT_BODY(SettleStage);
    return settle_(ctx);
}

IReplayStage::Step SettleStage::settle_(Ctx ctx) noexcept {
    const ReplayPlay::Request& req = ctx.request;
    const ReplayPlay::ScannedMovie& scan = ctx.scan;
    ReplayPlay::ArmPlan& arm = ctx.arm;
    ReplayPlay::Report& report = ctx.report;
    proto::StatusWord live{};
    if (ctx.w.core_status.sample_into(live) == 0)
        return Step::refuse(Refusal::NotWired);
    std::optional<proto::ItemTable> table{};
    if (ctx.w.conf.sample_into(conf_) != 0 && !conf_.truncated) {
        if (auto t = proto::ItemTable::parse(conf_.view())) {
            table = std::move(*t);
        }
    }
    const auto rows = table ? infra::OptRef<const proto::ItemTable>{*table}
                            : infra::OptRef<const proto::ItemTable>{};
    const auto asked = req.codec->core().setting_needs_for(scan.facts, req.ram_fill);
    if (!asked) {
        report.last_setting = "RAM init";
        report.last_setting_offered = false;
        return Step::refuse(Refusal::Setting);
    }
    const cores::IReplayCore::SettingNeeds needs = where_the_core_has(*asked, rows);
    const auto needs_view = needs.view();
    const auto unmet = std::find_if(needs_view.begin(), needs_view.end(), [&live](const auto& n) {
        return !n.met_by(live);
    });
    if (unmet != needs_view.end()) {

        if (waiting_ && ctx.w.clock.now() - since_ <= ReplayPlay::kAnswerNs)
            return Step::stay();
        report.last_setting = unmet->name;
        report.last_setting_offered = unmet->settable();
        if (waiting_ || !req.set_ok || !set_unmet_(ctx, live, needs, rows))
            return Step::refuse(Refusal::Setting);
        waiting_ = true;
        since_ = ctx.w.clock.now();
        return Step::stay();
    }

    const cores::ReplayLead fallback = req.codec->format().default_lead(scan.facts);
    const FrameCount codec_lead = FrameCount::of(fallback);
    if (!req.lead_override) {
        const cores::IReplayCore::LeadRange range = req.codec->core().lead_range();
        if (!range.admits(fallback))
            return Step::refuse(Refusal::Lead, LeadRefusal::of(codec_lead));
    }
    arm.lead = req.lead_override ? *req.lead_override : codec_lead;

    const std::optional<ArmLead> field = ArmLead::of(arm.lead);
    if (!field)
        return Step::refuse(Refusal::Lead, LeadRefusal::of(arm.lead));
    arm.lead_field = *field;
    const auto poweron = req.codec->core().power_on_ns(live, scan.facts);
    if (!poweron)
        return Step::refuse(Refusal::PowerOn);
    arm.poweron_ns = std::chrono::duration_cast<CodecNs>(*poweron);
    const cores::IReplayCore::PowerOn on = req.codec->core().power_on(scan.facts);
    arm.p0 = on.p0;
    arm.event = on.event;

    arm.rom_index = kNoRomSlot;
    if (arm.event == ReplayMsg::PowerOnEvent::LoadEnd) {
        const auto slot = rom_slot_(ctx);
        if (!slot)
            return Step::refuse(Refusal::Slot);
        arm.rom_index = *slot;
    }
    load_checkpoints_(ctx);
    ctx.w.diag.appendf(
        "{\"t\":\"tas\",\"k\":\"armed\",\"frames\":%u,\"ports\":%s,\"lead\":%s,"
        "\"phase_us\":%u,\"poweron_us\":%u,\"vsync_ok\":%u,\"checks\":%u,\"status\":"
        "\"%04x%04x%04x%04x%04x%04x%04x%04x\"}",
        scan.scanned,
        scan.facts.ports.text().c_str(),
        arm.lead.text().c_str(),
        scan.offset_us.count(),
        us_of(arm.poweron_ns).count(),
        scan.vsync_ok ? 1u : 0u,
        static_cast<unsigned>(arm.n_checks),
        live.words[7],
        live.words[6],
        live.words[5],
        live.words[4],
        live.words[3],
        live.words[2],
        live.words[1],
        live.words[0]
    );
    return Step::enter(Step::Next::Run);
}

bool SettleStage::set_unmet_(
    Ctx ctx,
    const proto::StatusWord& live,
    const cores::IReplayCore::SettingNeeds& needs,
    infra::OptRef<const proto::ItemTable> labels
) noexcept {
    ReplayPlay::Report& report = ctx.report;
    for (const auto& n : needs.view()) {
        if (!n.met_by(live) && !n.settable()) {
            report.last_setting = n.name;
            report.last_setting_offered = false;
            return false;
        }
    }
    const auto put = [&ctx](const cores::IReplayCore::SettingNeed& n, unsigned value) {
        return ctx.w.settings_tx.push(
            proto::LinkOp::WriteStatus{
                .start = n.field.start,
                .width = n.field.width,
                .value = value
            }
        );
    };
    for (const std::size_t k : needs.n.indices()) {
        const auto& n = needs.rows[k];
        if (n.met_by(live))
            continue;
        const unsigned was = proto::printed_field(n.value_in(live));
        if (!put(n, proto::printed_field(n.preferred))) {

            for (std::size_t j = 0; j < k; ++j) {
                const auto& m = needs.rows[j];
                if (!m.met_by(live))
                    (void)put(m, proto::printed_field(m.value_in(live)));
            }
            report.set.clear();
            report.last_setting = n.name;
            report.last_setting_offered = true;
            return false;
        }
        report.set.push_back(
            ReplayPlay::SetSetting{
                .name = n.name,
                .was = value_label(labels, n, was),
                .now = value_label(labels, n, proto::printed_field(n.preferred))
            }
        );
        ctx.w.diag.appendf(
            "{\"t\":\"tas\",\"k\":\"set\",\"setting\":\"%.*s\",\"lo\":%u,"
            "\"width\":%u,\"was\":%u,\"now\":%u}",
            static_cast<int>(n.name.size()),
            n.name.data(),
            proto::printed_bit(n.field.start),
            static_cast<unsigned>(n.field.width),
            was,
            static_cast<unsigned>(proto::printed_field(n.preferred))
        );
    }
    report.last_setting = {};
    return true;
}

void SettleStage::load_checkpoints_(Ctx ctx) noexcept {
    constexpr unsigned kDecimal = 10;
    const ReplayPlay::Request& req = ctx.request;
    ReplayPlay::ArmPlan& arm = ctx.arm;
    arm.n_checks = 0;
    arm.next_check = 0;
    char path[kPathMax + 8];
    const int n = std::snprintf(
        path,
        sizeof path,
        "%.*s.chk",
        static_cast<int>(req.movie.view().size()),
        req.movie.view().data()
    );
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof path)
        return;
    auto f = ctx.w.vfs->open(
        std::string_view(path, static_cast<std::size_t>(n)),
        svc::OpenMode::Read
    );
    if (!f)
        return;
    std::array<std::byte, 4096> buf;
    const auto got = (*f)->read_at(0, buf);
    if (!got)
        return;
    std::uint64_t v = 0;
    bool in = false;
    for (std::size_t i = 0; i <= *got && arm.n_checks < ReplayPlay::kCheckpoints; ++i) {
        const char c = i < *got ? static_cast<char>(buf[i]) : ' ';
        if (c >= '0' && c <= '9') {
            v = v * kDecimal + static_cast<unsigned>(c - '0');
            in = v <= ReplayPlay::kMaxFrames;
            if (!in)
                v = ReplayPlay::kMaxFrames + 1;
        } else {
            if (in)
                arm.checks[arm.n_checks++] = movie_frame_from_wire(static_cast<std::uint32_t>(v));
            v = 0;
            in = false;
        }
    }
    std::sort(arm.checks.begin(), arm.checks.begin() + static_cast<std::ptrdiff_t>(arm.n_checks));
}

std::optional<proto::IoIndex> SettleStage::rom_slot_(Ctx ctx) noexcept {
    if (ctx.w.conf.sample_into(conf_) == 0 || conf_.truncated)
        return std::nullopt;
    const auto table = proto::ItemTable::parse(conf_.view());
    if (!table)
        return std::nullopt;
    const std::vector<proto::ConfStrEntry> slots = proto::ConfStr::slots_of(*table);
    const proto::FileSlotDigit digit = ctx.request.codec->core().rom_slot_digit();
    const proto::FileSlotHit hit = proto::ConfStr::find_slot_in(
        slots,
        proto::ConfStrEntry::SlotLetter::Load,
        digit
    );
    if (!hit.entry)
        return std::nullopt;
    return hit.entry->ioctl_index;
}

}
