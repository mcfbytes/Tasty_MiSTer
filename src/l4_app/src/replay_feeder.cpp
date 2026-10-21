// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay_feeder.h"

#include <utility>

#include "app/hash_stage.h"
#include "app/identity_latch.h"
#include "app/run_stage.h"
#include "app/scan_stage.h"
#include "app/settle_stage.h"
#include "infra/diag_log.h"

namespace mister::app {

ReplayFeeder::ReplayFeeder(Wiring w) noexcept : w_(std::move(w)) {}

ReplayFeeder::~ReplayFeeder() = default;

std::unique_ptr<IReplayStage> ReplayFeeder::make_stage_(IReplayStage::Step::Next next) {
    switch (next) {
        case IReplayStage::Step::Next::Hash:
            return std::make_unique<HashStage>();
        case IReplayStage::Step::Next::Settle:
            return std::make_unique<SettleStage>();
        case IReplayStage::Step::Next::Run:
            return std::make_unique<RunStage>();
    }
    __builtin_unreachable();
}

void ReplayFeeder::refuse_(PlayRefusal why) noexcept {
    last_refusal_ = why.why;
    last_detail_ = why.detail;
    ++refusals_;
    active_.reset();
    play_.release_movie();
    w_.diag.appendf(
        "{\"t\":\"tas\",\"k\":\"refused\",\"why\":\"%s\",\"detail\":%u,\"setting\":\"%.*s\"}",
        PlayRefusal::name(why.why),
        why.detail,
        static_cast<int>(play_.last_setting.size()),
        play_.last_setting.data()
    );
    play_.log_end(w_.diag, ReplayStatus{.gen = play_.gen, .end = ReplayEnd::Refused});
}

void ReplayFeeder::apply_(IReplayStage::Step step) noexcept {
    drive_(step);
    stage_ = active_ ? active_->level() : Stage::Idle;
}

void ReplayFeeder::drive_(IReplayStage::Step step) noexcept {
    for (;;) {
        switch (step.kind) {
            case IReplayStage::Step::Kind::Stay:
                return;
            case IReplayStage::Step::Kind::Refuse:
                return refuse_(step.refusal);
            case IReplayStage::Step::Kind::Done:
                active_.reset();
                play_.release_movie();
                return;
            case IReplayStage::Step::Kind::Enter:
                active_ = make_stage_(step.next);
                step = active_->enter(ctx_());
                continue;
        }
    }
}

bool ReplayFeeder::take_play(const Play& p) noexcept {
    TASTY_SEAT_BODY(ReplayFeeder);
    play_.last_setting = {};
    play_.last_setting_offered = false;
    play_.refusal_path.clear();
    if (active_) {
        ++refusals_;
        last_refusal_ = Refusal::Busy;
        last_detail_ = 0;
        w_.diag.appendf("{\"t\":\"tas\",\"k\":\"refused\",\"why\":\"busy\"}");
        return true;
    }
    if (!w_.vfs) {
        refuse_(PlayRefusal{Refusal::NotWired});
        return true;
    }
    if (!play_.movie.assign(p.movie) || p.movie.empty()) {
        refuse_(PlayRefusal{Refusal::MovieIo});
        return true;
    }
    play_.codec = w_.identity.copy(ident_)
        ? infra::opt_ref_of(cores::movie_codec_for(ident_.rbf, p.movie))
        : infra::OptRef<const cores::IMovieCodec>{};
    if (!play_.codec) {
        refuse_(PlayRefusal{Refusal::NoCodec});
        return true;
    }
    if (!play_.rom.assign(p.rom) || p.rom.empty()) {
        refuse_(PlayRefusal{Refusal::RomIo});
        return true;
    }
    const cores::IMovieCodec::LeadRange range = play_.codec->lead_range();
    if (p.lead && (*p.lead < range.min || *p.lead > range.max)) {
        refuse_(PlayRefusal{Refusal::Lead, static_cast<std::uint32_t>(*p.lead)});
        return true;
    }
    if (p.stop_at && *p.stop_at == 0) {
        refuse_(
            PlayRefusal{
                Refusal::Movie,
                static_cast<std::uint32_t>(cores::IMovieCodec::Refusal::NotAMovie)
            }
        );
        return true;
    }
    play_.lead_override = p.lead;
    play_.stop_at = p.stop_at;
    play_.ram_fill = p.ram_fill;
    play_.seeded_save = p.seeded_save;
    play_.set_ok = p.set_settings;
    play_.set.clear();
    play_.offset_us = p.phase_us.value_or(0);
    active_ = std::make_unique<ScanStage>();
    apply_(active_->enter(ctx_()));
    return true;
}

bool ReplayFeeder::take_stop() noexcept {
    TASTY_SEAT_BODY(ReplayFeeder);
    if (active_)
        apply_(active_->stop(ctx_()));
    return true;
}

void ReplayFeeder::tick() noexcept {
    TASTY_SEAT_BODY(ReplayFeeder);
    if (active_)
        apply_(active_->tick(ctx_()));
}

}
