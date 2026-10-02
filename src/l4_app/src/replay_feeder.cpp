// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_feeder.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/identity_latch.h"
#include "app/input_wire.h"
#include "app/link_tx_channel.h"
#include "app/osd_close.h"
#include "app/screenshot_pump.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "app/video_pump.h"
#include "infra/diag_log.h"
#include "infra/message_sum.h"
#include "proto/conf_str.h"
#include "proto/item_table.h"
#include "proto/link_op.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

constexpr std::array<const char*, static_cast<std::size_t>(ReplayFeeder::Refusal::kCount)>
    kRefusalNames{"none",     "busy",     "not_wired", "no_codec", "movie_io",
                  "movie",    "rom_io",   "rom_size",  "checksum", "setting",
                  "phase",    "lead",     "command",   "too_long", "slot",
                  "power_on", "firmware", "rom_kind",  "rom_chd"};
static_assert(kRefusalNames.back() != nullptr, "every Refusal has a name");

}  // namespace

const char* ReplayFeeder::refusal_name(Refusal r) noexcept {
    const auto i = static_cast<std::size_t>(r);
    return i < kRefusalNames.size() ? kRefusalNames[i] : "?";
}

namespace {

std::string value_label(const proto::ItemTable* table, const cores::IMovieCodec::SettingNeed& need,
                        unsigned value) {
    if (table != nullptr) {
        for (const proto::Item& it : table->items()) {
            if (it.kind != proto::ItemKind::Option || it.bits.start.v != need.lo ||
                it.bits.width != need.width)
                continue;
            const std::string_view label = table->subfield(it, 2 + value);
            if (!label.empty()) return std::string(label);
            break;
        }
    }
    return std::to_string(value);
}

cores::IMovieCodec::SettingNeeds where_the_core_has(cores::IMovieCodec::SettingNeeds needs,
                                                    const proto::ItemTable* table) {
    if (table == nullptr) return needs;
    for (std::size_t i = 0; i < needs.n; ++i) {
        cores::IMovieCodec::SettingNeed& n = needs.rows[i];
        const proto::Item* at = nullptr;
        for (const proto::Item& it : table->items()) {
            if (it.kind != proto::ItemKind::Option || !it.bits.valid() ||
                table->subfield(it, 1) != n.name)
                continue;

            if (at == nullptr || (at->cond_count != 0 && it.cond_count == 0)) at = &it;
        }
        if (at == nullptr) continue;

        if (at->bits.width > 8 || n.preferred >= (1u << at->bits.width) ||
            n.preferred + 2u >= at->subfield_count) {
            n.allowed = 0;
            continue;
        }
        n.lo = at->bits.start.v;
        n.width = at->bits.width;
    }
    return needs;
}

}  // namespace

ReplayFeeder::ReplayFeeder(const Wiring& w) noexcept : w_(w) {}
ReplayFeeder::~ReplayFeeder() = default;

std::int64_t ReplayFeeder::now_ns_() const noexcept { return w_.clock->now().count(); }

void ReplayFeeder::refuse_(Refusal why, std::uint32_t detail) noexcept {
    last_refusal_ = why;
    last_detail_ = detail;
    ++refusals_;
    stage_ = Stage::Idle;
    file_.reset();
    rom_file_.reset();
    if (w_.diag != nullptr) {
        w_.diag->appendf(
            "{\"t\":\"tas\",\"k\":\"refused\",\"why\":\"%s\",\"detail\":%u,\"setting\":\"%.*s\"}",
            refusal_name(why), detail, static_cast<int>(last_setting_.size()),
            last_setting_.data());
    }
    log_end_(ReplayStatus{.gen = gen_, .end = ReplayEnd::Refused});
}

std::optional<ReplayStatus> ReplayFeeder::sample_status_() const noexcept {
    ReplayStatus s{};
    if (w_.status == nullptr || w_.status->sample_into(s) == 0) return std::nullopt;
    return s;
}

void ReplayFeeder::publish_(ReplayOp op) noexcept {
    w_.control->publish(ReplayControl{.gen = gen_, .op = op});
}

bool ReplayFeeder::take_play(const Play& p) noexcept {
    TASTY_SEAT_BODY(ReplayFeeder);
    last_setting_ = {};
    last_setting_offered_ = false;
    refusal_path_.clear();
    if (stage_ != Stage::Idle) {
        ++refusals_;
        last_refusal_ = Refusal::Busy;
        last_detail_ = 0;
        if (w_.diag != nullptr)
            w_.diag->appendf("{\"t\":\"tas\",\"k\":\"refused\",\"why\":\"busy\"}");
        return true;
    }
    if (w_.vfs == nullptr || w_.clock == nullptr || w_.ring == nullptr || w_.control == nullptr ||
        w_.status == nullptr || w_.identity == nullptr || w_.core_status == nullptr) {
        refuse_(Refusal::NotWired);
        return true;
    }
    if (!movie_.assign(p.movie) || p.movie.empty()) {
        refuse_(Refusal::MovieIo);
        return true;
    }
    codec_ = w_.identity->copy(ident_) ? cores::movie_codec_for(ident_.rbf, p.movie) : nullptr;
    if (codec_ == nullptr) {
        refuse_(Refusal::NoCodec);
        return true;
    }
    if (!rom_.assign(p.rom) || p.rom.empty()) {
        refuse_(Refusal::RomIo);
        return true;
    }
    const cores::IMovieCodec::LeadRange range = codec_->lead_range();
    if (p.lead && (*p.lead < range.min || *p.lead > range.max)) {
        refuse_(Refusal::Lead, static_cast<std::uint32_t>(*p.lead));
        return true;
    }
    if (p.stop_at && *p.stop_at == 0) {
        refuse_(Refusal::Movie, static_cast<std::uint32_t>(cores::IMovieCodec::Refusal::NotAMovie));
        return true;
    }
    lead_override_ = p.lead;
    stop_at_ = p.stop_at;
    ram_fill_ = p.ram_fill;
    seeded_save_ = p.seeded_save;
    set_ok_ = p.set_settings;
    set_.clear();
    offset_us_ = p.phase_us.value_or(0);
    auto f = codec_->open_movie(*w_.vfs, movie_.view());
    if (!f) {

        if (codec_->archive() != nullptr && f.error().code == Errc::bad_format)
            refuse_(Refusal::Movie, f.error().detail);
        else
            refuse_(Refusal::MovieIo, static_cast<std::uint32_t>(f.error().code));
        return true;
    }
    const auto size = (*f)->size();
    if (!size) {
        refuse_(Refusal::MovieIo, static_cast<std::uint32_t>(size.error().code));
        return true;
    }
    file_ = std::move(*f);
    file_size_ = size->v;
    stream_cut_ = false;
    off_ = 0;
    line_ = Line{};
    facts_ = {};
    in_log_ = false;
    log_done_ = false;
    frames_ = 0;
    movie_frames_ = 0;
    tries_ = 0;
    vsync_ok_ = w_.video != nullptr && w_.video->output_locked();
    stage_ = Stage::Scanning;
    return true;
}

bool ReplayFeeder::take_stop() noexcept {
    TASTY_SEAT_BODY(ReplayFeeder);
    switch (stage_) {
        case Stage::Idle:
        case Stage::Stopping:
            break;
        case Stage::Scanning:
        case Stage::Hashing:
        case Stage::Settling:
            stage_ = Stage::Idle;
            file_.reset();
            rom_file_.reset();
            break;
        case Stage::Arming:
        case Stage::Loading:
        case Stage::Streaming:
            publish_(ReplayOp::Stop);
            stage_ = Stage::Stopping;
            since_ns_ = now_ns_();
            break;
    }
    return true;
}

bool ReplayFeeder::read_lines_(bool scan) noexcept {
    std::array<std::byte, kReadChunk> buf;
    std::uint32_t spent = 0;
    while (spent < kTickBudget) {
        if (stream_cut_ || off_ >= file_size_) {
            if (line_.len != 0 || line_.clipped) {
                const Line l = line_;
                line_ = Line{};
                if (!on_line_(std::string_view(l.text.data(), l.len), l.clipped, l.start, scan))
                    return false;
            }
            if (scan) {
                finish_scan_();
            } else if (flush_() && !end_pushed_) {
                if (run_ && !push_run_()) return false;
                run_.reset();
                end_pushed_ = w_.ring->push(infra::make<ReplayMsg>(ReplayMsg::End{.frame = frames_},
                                                                   ReplayMsg::Head{gen_}));
            }
            return false;
        }
        const auto got = file_->read_at(off_, buf);
        if (!got || *got == 0) {
            if (scan) {
                if (!got && got.error().code == Errc::bad_format && codec_->archive() != nullptr)
                    refuse_(Refusal::Movie, got.error().detail);
                else
                    refuse_(Refusal::MovieIo,
                            got ? 0u : static_cast<std::uint32_t>(got.error().code));
                return false;
            }
            file_size_ = off_;
            continue;
        }
        spent += static_cast<std::uint32_t>(*got);
        for (std::size_t i = 0; i < *got; ++i) {
            const char c = static_cast<char>(buf[i]);
            const std::uint64_t at = off_ + i;
            if (line_.len == 0 && !line_.clipped) line_.start = at;
            if (c == '\n') {
                const Line l = line_;
                line_ = Line{};
                if (!on_line_(std::string_view(l.text.data(), l.len), l.clipped, l.start, scan)) {
                    off_ = at + 1;
                    return false;
                }
                continue;
            }
            if (line_.len < line_.text.size()) {
                line_.text[line_.len++] = c;
            } else {
                line_.clipped = true;
            }
        }
        off_ += *got;
    }
    return true;
}

bool ReplayFeeder::on_line_(std::string_view line, bool clipped, std::uint64_t start,
                            bool scan) noexcept {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty() && !clipped) return true;
    if (in_log_ && (log_done_ || codec_->ends_log(line))) {
        log_done_ = true;
        return true;
    }
    return scan ? scan_line_(line, clipped, start) : stream_line_(line);
}

bool ReplayFeeder::scan_line_(std::string_view line, bool clipped, std::uint64_t start) noexcept {
    using CR = cores::IMovieCodec::Refusal;
    if (!in_log_) {
        if (!codec_->starts_log(line)) {
            const auto f = codec_->header_line(line, facts_);
            if (!f) {
                refuse_(Refusal::Movie, f.error().detail);
                return false;
            }
            facts_ = *f;
            return true;
        }
        if (const auto e = codec_->finish_header(facts_); !e) {
            refuse_(Refusal::Movie, e.error().detail);
            return false;
        }
        in_log_ = true;
        log_start_ = start;
    }
    if (clipped) {
        refuse_(Refusal::Movie, static_cast<std::uint32_t>(CR::BadLine));
        return false;
    }
    const auto fr = codec_->frame(line, facts_);
    if (!fr) {
        refuse_(Refusal::Movie, fr.error().detail);
        return false;
    }

    if (frames_ > 0 && fr->commands != 0) {
        refuse_(Refusal::Command, frames_);
        return false;
    }
    if (++frames_ > kMaxFrames) {
        refuse_(Refusal::TooLong, frames_);
        return false;
    }
    return true;
}

void ReplayFeeder::finish_scan_() noexcept {
    if (stage_ != Stage::Scanning) return;
    if (!in_log_ || frames_ == 0) {
        return refuse_(Refusal::Movie,
                       static_cast<std::uint32_t>(cores::IMovieCodec::Refusal::NotAMovie));
    }
    movie_frames_ = stop_at_ ? std::min(frames_, *stop_at_) : frames_;
    const cores::IMovieCodec::Raster r = codec_->raster(facts_);
    if (offset_us_ == 0) offset_us_ = r.period_ns / 2000;
    if (!phase_fits(offset_us_, r.period_ns)) return refuse_(Refusal::Phase, offset_us_);

    const auto src = codec_->digest_source(*w_.vfs, rom_.view());
    if (!src) {

        using CR = cores::IMovieCodec::Refusal;
        const auto why = static_cast<CR>(src.error().detail);
        if (src.error().code == Errc::bad_format)
            return refuse_(why == CR::Disk             ? Refusal::RomKind
                           : why == CR::ChdUnsupported ? Refusal::RomChd
                                                       : Refusal::RomSize,
                           src.error().detail);
        return refuse_(Refusal::RomIo, static_cast<std::uint32_t>(src.error().code));
    }
    auto rom = w_.vfs->open(src->file, svc::OpenMode::Read);
    if (!rom) return refuse_(Refusal::RomIo, static_cast<std::uint32_t>(rom.error().code));
    rom_file_ = std::move(*rom);
    rom_off_ = src->span.offset;

    rom_end_ = facts_.has_digest ? src->span.offset + src->span.length : rom_off_;
    digest_ = cores::RomDigest{facts_.digest.kind};
    if (facts_.has_digest) digest_.update(src->prefix);
    has_alt_ = facts_.has_digest && src->alt_prefix.has_value();
    if (has_alt_) {
        digest_alt_ = cores::RomDigest{facts_.digest.kind};
        digest_alt_.update(*src->alt_prefix);
    }
    firmware_pass_ = false;
    stage_ = Stage::Hashing;
}

void ReplayFeeder::hash_rom_() noexcept {
    std::array<std::uint8_t, kReadChunk> buf;
    std::uint32_t spent = 0;
    while (spent < kTickBudget && rom_off_ < rom_end_) {
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), rom_end_ - rom_off_));
        const std::span<std::uint8_t> chunk(buf.data(), want);
        const auto got = rom_file_->read_at(rom_off_, std::as_writable_bytes(chunk));
        if (!got || *got == 0 || *got > want)
            return refuse_(firmware_pass_ ? Refusal::Firmware : Refusal::RomIo);
        digest_.update(chunk.first(*got));
        if (has_alt_) digest_alt_.update(chunk.first(*got));
        rom_off_ += *got;
        spent += static_cast<std::uint32_t>(*got);
    }
    if (rom_off_ < rom_end_) return;
    rom_file_.reset();
    finish_rom_();
}

void ReplayFeeder::finish_rom_() noexcept {
    if (firmware_pass_) {
        if (digest_.finish() != facts_.firmware) return refuse_(Refusal::Firmware, 1);
        refusal_path_.clear();
        return settle_();
    }
    const bool alt = has_alt_ && codec_->rom_matches(digest_alt_.finish(), facts_);
    has_alt_ = false;
    if (!codec_->rom_matches(digest_.finish(), facts_) && !alt) return refuse_(Refusal::Checksum);
    const auto c = facts_.has_firmware ? codec_->companion(rom_.view(), facts_) : std::nullopt;
    if (facts_.has_firmware && !c) return refuse_(Refusal::Firmware);
    if (!c) return settle_();

    for (const std::string& path : c->paths) {
        if (path.empty()) continue;
        if (refusal_path_.empty()) refusal_path_ = path;
        auto f = w_.vfs->open(path, svc::OpenMode::Read);
        if (!f) continue;
        const auto size = (*f)->size();
        if (!size || size->v != c->size) continue;
        rom_file_ = std::move(*f);
        rom_off_ = 0;
        rom_end_ = size->v;
        digest_ = cores::RomDigest{facts_.firmware.kind};
        firmware_pass_ = true;
        refusal_path_ = path;
        return;
    }
    return refuse_(Refusal::Firmware);
}

void ReplayFeeder::settle_() noexcept {
    proto::StatusWord live{};
    if (w_.core_status->sample_into(live) == 0) return refuse_(Refusal::NotWired);
    std::optional<proto::ItemTable> table{};
    if (w_.conf != nullptr && w_.conf->sample_into(conf_) != 0 && !conf_.truncated) {
        if (auto t = proto::ItemTable::parse(std::string_view(conf_.text, conf_.len))) {
            table = std::move(*t);
        }
    }
    const proto::ItemTable* rows = table ? &*table : nullptr;
    const auto asked = codec_->setting_needs_for(facts_, ram_fill_);
    if (!asked) {
        last_setting_ = "RAM init";
        last_setting_offered_ = false;
        return refuse_(Refusal::Setting);
    }
    const cores::IMovieCodec::SettingNeeds needs = where_the_core_has(*asked, rows);
    const auto needs_view = needs.view();
    const auto unmet = std::find_if(needs_view.begin(), needs_view.end(),
                                    [&live](const auto& n) { return !n.met_by(live); });
    if (unmet != needs_view.end()) {

        if (stage_ == Stage::Settling && now_ns_() - since_ns_ <= kAnswerNs) return;
        last_setting_ = unmet->name;
        last_setting_offered_ = unmet->settable();
        if (stage_ == Stage::Settling || !set_ok_ || w_.settings_tx == nullptr ||
            !set_unmet_(live, needs, rows))
            return refuse_(Refusal::Setting);
        stage_ = Stage::Settling;
        since_ns_ = now_ns_();
        return;
    }
    lead_ = lead_override_.value_or(codec_->default_lead(facts_));
    const cores::IMovieCodec::LeadRange range = codec_->lead_range();

    if (lead_ < range.min || lead_ > range.max || lead_ < INT16_MIN || lead_ > INT16_MAX)
        return refuse_(Refusal::Lead, static_cast<std::uint32_t>(lead_));
    const auto poweron = codec_->power_on_ns(live, facts_);
    if (!poweron) return refuse_(Refusal::PowerOn);
    poweron_ns_ = *poweron;
    const cores::IMovieCodec::PowerOn on = codec_->power_on(facts_);
    switch (on.p0) {
        case cores::IMovieCodec::Parity::Any:
            p0_ = ReplayMsg::P0Parity::Any;
            break;
        case cores::IMovieCodec::Parity::Even:
            p0_ = ReplayMsg::P0Parity::Even;
            break;
        case cores::IMovieCodec::Parity::Odd:
            p0_ = ReplayMsg::P0Parity::Odd;
            break;
        case cores::IMovieCodec::Parity::AfterSilence:
            p0_ = ReplayMsg::P0Parity::AfterSilence;
            break;
    }
    event_ = on.event == cores::IMovieCodec::PowerOnEvent::ResetPulse
                 ? ReplayMsg::PowerOnEvent::ResetPulse
                 : ReplayMsg::PowerOnEvent::LoadEnd;

    rom_index_ = 0xFF;
    if (event_ == ReplayMsg::PowerOnEvent::LoadEnd) {
        const auto slot = rom_slot_();
        if (!slot) return refuse_(Refusal::Slot);
        rom_index_ = *slot;
    }
    load_checkpoints_();
    if (w_.diag != nullptr) {
        w_.diag->appendf(
            "{\"t\":\"tas\",\"k\":\"armed\",\"frames\":%u,\"ports\":%u,\"lead\":%d,"
            "\"phase_us\":%u,\"poweron_us\":%u,\"vsync_ok\":%u,\"checks\":%u,\"status\":"
            "\"%04x%04x%04x%04x%04x%04x%04x%04x\"}",
            frames_, static_cast<unsigned>(facts_.ports), static_cast<int>(lead_), offset_us_,
            poweron_ns_ / 1000u, vsync_ok_ ? 1u : 0u, static_cast<unsigned>(n_checks_),
            live.words[7], live.words[6], live.words[5], live.words[4], live.words[3],
            live.words[2], live.words[1], live.words[0]);
    }
    arm_();
}

bool ReplayFeeder::set_unmet_(const proto::StatusWord& live,
                              const cores::IMovieCodec::SettingNeeds& needs,
                              const proto::ItemTable* labels) noexcept {
    for (const auto& n : needs.view()) {
        if (!n.met_by(live) && !n.settable()) {
            last_setting_ = n.name;
            last_setting_offered_ = false;
            return false;
        }
    }
    const auto put = [this](const cores::IMovieCodec::SettingNeed& n, unsigned value) {
        return w_.settings_tx->push(proto::LinkOp::WriteStatus{
            .start = proto::StatusBit{n.lo}, .width = n.width, .value = value});
    };
    for (std::size_t k = 0; k < needs.n; ++k) {
        const auto& n = needs.rows[k];
        if (n.met_by(live)) continue;
        const unsigned was = n.value_in(live);
        if (!put(n, n.preferred)) {

            for (std::size_t j = 0; j < k; ++j) {
                const auto& m = needs.rows[j];
                if (!m.met_by(live)) (void)put(m, m.value_in(live));
            }
            set_.clear();
            last_setting_ = n.name;
            last_setting_offered_ = true;
            return false;
        }
        set_.push_back(SetSetting{.name = n.name,
                                  .was = value_label(labels, n, was),
                                  .now = value_label(labels, n, n.preferred)});
        if (w_.diag != nullptr) {
            w_.diag->appendf("{\"t\":\"tas\",\"k\":\"set\",\"setting\":\"%.*s\",\"lo\":%u,"
                             "\"width\":%u,\"was\":%u,\"now\":%u}",
                             static_cast<int>(n.name.size()), n.name.data(),
                             static_cast<unsigned>(n.lo), static_cast<unsigned>(n.width), was,
                             static_cast<unsigned>(n.preferred));
        }
    }
    last_setting_ = {};
    return true;
}

void ReplayFeeder::load_checkpoints_() noexcept {
    n_checks_ = 0;
    next_check_ = 0;
    char path[kPathMax + 8];
    const int n = std::snprintf(path, sizeof path, "%.*s.chk",
                                static_cast<int>(movie_.view().size()), movie_.view().data());
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof path) return;
    auto f = w_.vfs->open(std::string_view(path, static_cast<std::size_t>(n)), svc::OpenMode::Read);
    if (!f) return;
    std::array<std::byte, 4096> buf;
    const auto got = (*f)->read_at(0, buf);
    if (!got) return;
    std::uint64_t v = 0;
    bool in = false;
    for (std::size_t i = 0; i <= *got && n_checks_ < kCheckpoints; ++i) {
        const char c = i < *got ? static_cast<char>(buf[i]) : ' ';
        if (c >= '0' && c <= '9') {
            v = v * 10 + static_cast<unsigned>(c - '0');
            in = v <= kMaxFrames;
            if (!in) v = kMaxFrames + 1;
        } else {
            if (in) checks_[n_checks_++] = static_cast<std::uint32_t>(v);
            v = 0;
            in = false;
        }
    }
    std::sort(checks_.begin(), checks_.begin() + static_cast<std::ptrdiff_t>(n_checks_));
}

std::optional<std::uint8_t> ReplayFeeder::rom_slot_() noexcept {
    if (w_.conf == nullptr) return std::uint8_t{0xFF};
    if (w_.conf->sample_into(conf_) == 0 || conf_.truncated) return std::nullopt;
    const auto table = proto::ItemTable::parse(std::string_view(conf_.text, conf_.len));
    if (!table) return std::nullopt;
    const std::vector<proto::ConfStrEntry> slots = proto::ConfStr::slots_of(*table);
    const proto::FileSlotHit hit = proto::ConfStr::find_slot_in(slots, 'F', codec_->rom_digit());
    if (hit.entry == nullptr) return std::nullopt;
    return hit.entry->ioctl_index;
}

void ReplayFeeder::arm_() noexcept {
    ++gen_;
    if (gen_ == 0) gen_ = 1;
    run_.reset();
    next_.reset();
    end_pushed_ = false;
    frames_ = 0;
    off_ = log_start_;
    line_ = Line{};
    log_done_ = false;
    stream_cut_ = false;
    stop_asked_ = false;

    publish_(ReplayOp::Play);
    const cores::IMovieCodec::Raster r = codec_->raster(facts_);
    const ReplayMsg::Arm a{.ports = facts_.ports,
                           .rom_index = rom_index_,
                           .vsync_ok = static_cast<std::uint8_t>(vsync_ok_ ? 1 : 0),
                           .p0 = p0_,
                           .lead = static_cast<std::int16_t>(lead_),
                           .event = event_,
                           .late_frames = codec_->power_on(facts_).late_frames,
                           .offset_us = offset_us_,
                           .period_ns = r.period_ns,
                           .line0_ns = r.line0_ns,
                           .poweron_ns = poweron_ns_};
    if (!w_.ring->push(infra::make<ReplayMsg>(a, ReplayMsg::Head{gen_}))) {
        publish_(ReplayOp::Stop);
        return refuse_(Refusal::Busy);
    }
    stage_ = Stage::Arming;
    since_ns_ = now_ns_();
}

bool ReplayFeeder::push_run_() noexcept {
    const ReplayMsg::Input in{.first = run_->first, .last = run_->last, .mask = run_->mask};
    return w_.ring->push(infra::make<ReplayMsg>(in, ReplayMsg::Head{gen_}));
}

bool ReplayFeeder::flush_() noexcept {
    if (!next_) return true;
    if (!push_run_()) return false;
    run_ = next_;
    next_.reset();
    return true;
}

bool ReplayFeeder::stream_line_(std::string_view line) noexcept {

    if (stop_at_ && frames_ >= *stop_at_) {
        log_done_ = true;
        stream_cut_ = true;
        return true;
    }
    const auto fr = codec_->frame(line, facts_);
    if (!fr) {

        file_size_ = 0;
        return false;
    }
    const std::uint32_t f = frames_;
    std::array<std::uint32_t, kReplayPorts> m{};
    for (std::size_t p = 0; p < kReplayPorts && p < fr->mask.size(); ++p)
        m[p] = fr->mask[p];
    ++frames_;
    if (!run_) {
        run_ = Run{f, f, m};
        return true;
    }
    if (run_->mask == m && run_->last - run_->first + 1 < kRunSplit) {
        run_->last = f;
        return true;
    }
    if (!push_run_()) {
        next_ = Run{f, f, m};
        return false;
    }
    run_ = Run{f, f, m};
    return true;
}

void ReplayFeeder::tick() noexcept {
    TASTY_SEAT_BODY(ReplayFeeder);
    if (stage_ == Stage::Idle) return;
    if (stage_ == Stage::Scanning) {
        (void)read_lines_(true);
        return;
    }
    if (stage_ == Stage::Hashing) return hash_rom_();
    if (stage_ == Stage::Settling) return settle_();
    const auto s = sample_status_();
    const bool fresh = s && s->gen == gen_;
    if (fresh && s->level == ReplayLevel::Idle && s->end != ReplayEnd::None) return finish_(*s);
    if (stage_ == Stage::Stopping) {
        if (now_ns_() - since_ns_ > kAnswerNs)
            finish_(ReplayStatus{.gen = gen_, .end = ReplayEnd::Stopped});
        return;
    }
    if (stage_ == Stage::Arming || stage_ == Stage::Loading)
        tick_arming_(s.value_or(ReplayStatus{}), fresh);
    if (stage_ == Stage::Idle || stage_ == Stage::Stopping) return;
    if (s) checkpoints_(*s);
    if (flush_() && !end_pushed_) (void)read_lines_(false);
}

void ReplayFeeder::tick_arming_(const ReplayStatus& s, bool fresh) noexcept {
    if (stage_ == Stage::Arming) {
        if (fresh && s.level == ReplayLevel::AwaitPowerOn) {

            CoreScope scope{};
            if (w_.conf != nullptr && w_.conf->sample_into(conf_) != 0)
                scope = CoreScope{conf_.gen};
            bool asked = false;
            if (w_.asks != nullptr && event_ == ReplayMsg::PowerOnEvent::ResetPulse) {

                const UiRequest::ResetCore ask{
                    .edge = proto::ResetEdge::osd_toggle(proto::StatusBit{0}, false),
                    .scope = scope};
                asked = w_.asks->push(ask) != kUncaused;
            } else if (w_.asks != nullptr) {

                const cores::RamImageRecipe ram = codec_->power_on_ram(facts_);
                const std::size_t need = ram.size() == 0 ? 1 : 2;
                const bool image_asked =
                    UiRequestRing::kSlots - w_.asks->size() >= need &&
                    (ram.size() == 0 || w_.asks->push(UiRequest::LoadRamImage{
                                            .scope = scope, .recipe = ram}) != kUncaused);
                UiRequest::LoadFileByDigit ask{};
                ask.digit = proto::FileSlotDigit{codec_->rom_digit()};
                ask.save = seeded_save_ ? UiRequest::SaveChoice::ReplaySeeded
                                        : UiRequest::SaveChoice::ReplayFresh;
                ask.scope = scope;
                (void)ask.path.assign(rom_.view());
                asked = image_asked && w_.asks->push(ask) != kUncaused;
            }
            if (!asked) {
                publish_(ReplayOp::Stop);
                stage_ = Stage::Stopping;
                since_ns_ = now_ns_();
                return;
            }
            stage_ = Stage::Loading;
            since_ns_ = now_ns_();
            if (w_.osd != nullptr) w_.osd->close_osd();
        } else if (now_ns_() - since_ns_ > kAnswerNs) {
            publish_(ReplayOp::Stop);
            finish_(ReplayStatus{.gen = gen_, .end = ReplayEnd::Refused});
        }
        return;
    }
    if (fresh && s.level == ReplayLevel::Running) {
        stage_ = Stage::Streaming;
    } else if (now_ns_() - since_ns_ > kPowerOnNs) {
        publish_(ReplayOp::Stop);
        stage_ = Stage::Stopping;
        since_ns_ = now_ns_();
    }
}

void ReplayFeeder::checkpoints_(const ReplayStatus& s) noexcept {
    if (s.gen != gen_ || w_.shots == nullptr) return;
    while (next_check_ < n_checks_ && s.movie_frame >= 0 &&
           static_cast<std::uint32_t>(s.movie_frame) >= checks_[next_check_]) {
        char name[48];
        const int n = std::snprintf(name, sizeof name, "tas-%u-%u.png", static_cast<unsigned>(gen_),
                                    checks_[next_check_]);
        if (n > 0) (void)w_.shots->take(std::string_view(name, static_cast<std::size_t>(n)), false);
        ++next_check_;
    }
}

void ReplayFeeder::log_end_(const ReplayStatus& s) noexcept {
    if (w_.diag == nullptr) return;
    w_.diag->appendf("{\"t\":\"tas\",\"k\":\"end\",%s,\"tries\":%u}",
                     format_replay_status(s).c_str(), tries_);
}

void ReplayFeeder::finish_(const ReplayStatus& s) noexcept {
    log_end_(s);
    if (s.end == ReplayEnd::EpochAmbiguous && tries_ + 1 < kEpochTries &&
        stage_ != Stage::Stopping) {
        ++tries_;
        return arm_();
    }

    if (w_.input != nullptr) w_.input->publish_edge_reset();
    stage_ = Stage::Idle;
    file_.reset();
}

}  // namespace mister::app
