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
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

using Refusal = PlayRefusal::Why;

std::string value_label(
    infra::OptRef<const proto::ItemTable> table,
    const cores::IMovieCodec::SettingNeed& need,
    unsigned value
) {
    if (table) {
        for (const proto::Item& it : table->items()) {
            if (it.kind != proto::ItemKind::Option
                || it.bits.start.v != need.lo
                || it.bits.width != need.width)
                continue;
            const std::string_view label = table->subfield(it, 2 + value);
            if (!label.empty())
                return std::string(label);
            break;
        }
    }
    return std::to_string(value);
}

cores::IMovieCodec::SettingNeeds where_the_core_has(
    cores::IMovieCodec::SettingNeeds needs,
    infra::OptRef<const proto::ItemTable> table
) {
    if (!table)
        return needs;
    for (std::size_t i = 0; i < needs.n; ++i) {
        cores::IMovieCodec::SettingNeed& n = needs.rows[i];
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

        if (at->bits.width > 8
            || n.preferred >= (1u << at->bits.width)
            || n.preferred + 2u >= at->subfield_count) {
            n.allowed = 0;
            continue;
        }
        n.lo = at->bits.start.v;
        n.width = at->bits.width;
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
    ReplayPlay& play = ctx.play;
    proto::StatusWord live{};
    if (ctx.w.core_status.sample_into(live) == 0)
        return Step::refuse(Refusal::NotWired);
    std::optional<proto::ItemTable> table{};
    if (ctx.w.conf.sample_into(conf_) != 0 && !conf_.truncated) {
        if (auto t = proto::ItemTable::parse(std::string_view(conf_.text, conf_.len))) {
            table = std::move(*t);
        }
    }
    const auto rows = table ? infra::OptRef<const proto::ItemTable>{*table}
                            : infra::OptRef<const proto::ItemTable>{};
    const auto asked = play.codec->setting_needs_for(play.facts, play.ram_fill);
    if (!asked) {
        play.last_setting = "RAM init";
        play.last_setting_offered = false;
        return Step::refuse(Refusal::Setting);
    }
    const cores::IMovieCodec::SettingNeeds needs = where_the_core_has(*asked, rows);
    const auto needs_view = needs.view();
    const auto unmet = std::find_if(needs_view.begin(), needs_view.end(), [&live](const auto& n) {
        return !n.met_by(live);
    });
    if (unmet != needs_view.end()) {

        if (waiting_ && ctx.now_ns() - since_ns_ <= ReplayPlay::kAnswerNs)
            return Step::stay();
        play.last_setting = unmet->name;
        play.last_setting_offered = unmet->settable();
        if (waiting_ || !play.set_ok || !set_unmet_(ctx, live, needs, rows))
            return Step::refuse(Refusal::Setting);
        waiting_ = true;
        since_ns_ = ctx.now_ns();
        return Step::stay();
    }
    play.lead = play.lead_override.value_or(play.codec->default_lead(play.facts));
    const cores::IMovieCodec::LeadRange range = play.codec->lead_range();

    if (play.lead < range.min
        || play.lead > range.max
        || play.lead < INT16_MIN
        || play.lead > INT16_MAX)
        return Step::refuse(Refusal::Lead, static_cast<std::uint32_t>(play.lead));
    const auto poweron = play.codec->power_on_ns(live, play.facts);
    if (!poweron)
        return Step::refuse(Refusal::PowerOn);
    play.poweron_ns = *poweron;
    const cores::IMovieCodec::PowerOn on = play.codec->power_on(play.facts);
    switch (on.p0) {
        case cores::IMovieCodec::Parity::Any:
            play.p0 = ReplayMsg::P0Parity::Any;
            break;
        case cores::IMovieCodec::Parity::Even:
            play.p0 = ReplayMsg::P0Parity::Even;
            break;
        case cores::IMovieCodec::Parity::Odd:
            play.p0 = ReplayMsg::P0Parity::Odd;
            break;
        case cores::IMovieCodec::Parity::AfterSilence:
            play.p0 = ReplayMsg::P0Parity::AfterSilence;
            break;
    }
    play.event = on.event == cores::IMovieCodec::PowerOnEvent::ResetPulse
        ? ReplayMsg::PowerOnEvent::ResetPulse
        : ReplayMsg::PowerOnEvent::LoadEnd;

    play.rom_index = 0xFF;
    if (play.event == ReplayMsg::PowerOnEvent::LoadEnd) {
        const auto slot = rom_slot_(ctx);
        if (!slot)
            return Step::refuse(Refusal::Slot);
        play.rom_index = *slot;
    }
    load_checkpoints_(ctx);
    ctx.w.diag.appendf(
        "{\"t\":\"tas\",\"k\":\"armed\",\"frames\":%u,\"ports\":%u,\"lead\":%d,"
        "\"phase_us\":%u,\"poweron_us\":%u,\"vsync_ok\":%u,\"checks\":%u,\"status\":"
        "\"%04x%04x%04x%04x%04x%04x%04x%04x\"}",
        play.frames,
        static_cast<unsigned>(play.facts.ports),
        static_cast<int>(play.lead),
        play.offset_us,
        play.poweron_ns / 1000u,
        play.vsync_ok ? 1u : 0u,
        static_cast<unsigned>(play.n_checks),
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
    const cores::IMovieCodec::SettingNeeds& needs,
    infra::OptRef<const proto::ItemTable> labels
) noexcept {
    ReplayPlay& play = ctx.play;
    for (const auto& n : needs.view()) {
        if (!n.met_by(live) && !n.settable()) {
            play.last_setting = n.name;
            play.last_setting_offered = false;
            return false;
        }
    }
    const auto put = [&ctx](const cores::IMovieCodec::SettingNeed& n, unsigned value) {
        return ctx.w.settings_tx.push(
            proto::LinkOp::WriteStatus{
                .start = proto::StatusBit{n.lo},
                .width = n.width,
                .value = value
            }
        );
    };
    for (std::size_t k = 0; k < needs.n; ++k) {
        const auto& n = needs.rows[k];
        if (n.met_by(live))
            continue;
        const unsigned was = n.value_in(live);
        if (!put(n, n.preferred)) {

            for (std::size_t j = 0; j < k; ++j) {
                const auto& m = needs.rows[j];
                if (!m.met_by(live))
                    (void)put(m, m.value_in(live));
            }
            play.set.clear();
            play.last_setting = n.name;
            play.last_setting_offered = true;
            return false;
        }
        play.set.push_back(
            ReplayPlay::SetSetting{
                .name = n.name,
                .was = value_label(labels, n, was),
                .now = value_label(labels, n, n.preferred)
            }
        );
        ctx.w.diag.appendf(
            "{\"t\":\"tas\",\"k\":\"set\",\"setting\":\"%.*s\",\"lo\":%u,"
            "\"width\":%u,\"was\":%u,\"now\":%u}",
            static_cast<int>(n.name.size()),
            n.name.data(),
            static_cast<unsigned>(n.lo),
            static_cast<unsigned>(n.width),
            was,
            static_cast<unsigned>(n.preferred)
        );
    }
    play.last_setting = {};
    return true;
}

void SettleStage::load_checkpoints_(Ctx ctx) noexcept {
    ReplayPlay& play = ctx.play;
    play.n_checks = 0;
    play.next_check = 0;
    char path[kPathMax + 8];
    const int n = std::snprintf(
        path,
        sizeof path,
        "%.*s.chk",
        static_cast<int>(play.movie.view().size()),
        play.movie.view().data()
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
    for (std::size_t i = 0; i <= *got && play.n_checks < ReplayPlay::kCheckpoints; ++i) {
        const char c = i < *got ? static_cast<char>(buf[i]) : ' ';
        if (c >= '0' && c <= '9') {
            v = v * 10 + static_cast<unsigned>(c - '0');
            in = v <= ReplayPlay::kMaxFrames;
            if (!in)
                v = ReplayPlay::kMaxFrames + 1;
        } else {
            if (in)
                play.checks[play.n_checks++] = static_cast<std::uint32_t>(v);
            v = 0;
            in = false;
        }
    }
    std::sort(
        play.checks.begin(),
        play.checks.begin() + static_cast<std::ptrdiff_t>(play.n_checks)
    );
}

std::optional<std::uint8_t> SettleStage::rom_slot_(Ctx ctx) noexcept {
    if (ctx.w.conf.sample_into(conf_) == 0 || conf_.truncated)
        return std::nullopt;
    const auto table = proto::ItemTable::parse(std::string_view(conf_.text, conf_.len));
    if (!table)
        return std::nullopt;
    const std::vector<proto::ConfStrEntry> slots = proto::ConfStr::slots_of(*table);
    const proto::FileSlotHit hit = proto::ConfStr::find_slot_in(
        slots,
        'F',
        ctx.play.codec->rom_digit()
    );
    if (!hit.entry)
        return std::nullopt;
    return hit.entry->ioctl_index;
}

}
