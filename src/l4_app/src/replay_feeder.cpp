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
    const std::string_view setting = play_.report.last_setting;
    w_.diag.appendf(
        "{\"t\":\"tas\",\"k\":\"refused\",\"why\":\"%s\",\"detail\":%u,\"setting\":\"%.*s\"}",
        PlayRefusal::name(why.why),
        why.detail,
        static_cast<int>(setting.size()),
        setting.data()
    );
    log_end(w_.diag, ReplayStatus{.gen = play_.run.gen, .end = ReplayEnd::Refused}, play_.run);
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
    play_.report.last_setting = {};
    play_.report.last_setting_offered = false;
    play_.report.refusal_path.clear();
    if (active_) {
        ++refusals_;
        last_refusal_ = Refusal::Busy;
        last_detail_ = 0;
        w_.diag.appendf("{\"t\":\"tas\",\"k\":\"refused\",\"why\":\"busy\"}");
        return true;
    }

    play_.run.tries = 0;
    if (!w_.vfs) {
        refuse_(PlayRefusal{Refusal::NotWired});
        return true;
    }
    if (!play_.request.movie.assign(p.movie) || p.movie.empty()) {
        refuse_(PlayRefusal{Refusal::MovieIo});
        return true;
    }
    const std::optional<SessionIdentity> id = w_.identity.copy();
    play_.request.codec = id ? cores::movie_codec_for(id->rbf.view(), p.movie)
                             : infra::OptRef<const cores::MovieCodec>{};
    if (!play_.request.codec) {
        refuse_(PlayRefusal{Refusal::NoCodec});
        return true;
    }
    if (!play_.request.rom.assign(p.rom) || p.rom.empty()) {
        refuse_(PlayRefusal{Refusal::RomIo});
        return true;
    }
    const cores::IReplayCore::LeadRange range = play_.request.codec->core().lead_range();

    if (p.lead && !range.admits(p.lead->as_lead())) {
        refuse_(LeadRefusal::of(*p.lead).refused(Refusal::Lead));
        return true;
    }
    if (p.stop_at && *p.stop_at == MovieFrame{}) {
        refuse_(
            PlayRefusal{Refusal::Movie, static_cast<std::uint32_t>(cores::MovieRefusal::NotAMovie)}
        );
        return true;
    }
    play_.request.lead_override = p.lead;
    play_.request.stop_at = p.stop_at;
    play_.request.ram_fill = p.ram_fill;
    play_.request.seeded_save = p.seeded_save;
    play_.request.set_ok = p.set_settings;
    play_.request.offset_us = p.phase ? saturated<ArmMicros>(*p.phase) : ArmMicros{};
    play_.report.set.clear();
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
