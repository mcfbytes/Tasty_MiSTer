// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_upload_relay.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <utility>

#include "infra/message_sum.h"

namespace mister::app {

namespace {

[[nodiscard]] constexpr proto::SaveAct done(bool ok) noexcept {
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Done, .ok = ok};
}

}

void SaveUploadRelay::install(std::unique_ptr<cores::ISaveServant> servant) noexcept {
    io_.servant = std::move(servant);
}

bool SaveUploadRelay::order(const cores::SaveImage& image, std::string_view rel) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, A);
    if (image.size == 0 || rel.empty() || !rt_.order.rel.assign(rel)) {
        rt_.refused.add();
        return false;
    }
    rt_.order.image = image;
    rt_.order.owed = true;
    return true;
}

void SaveUploadRelay::service_rt(
    hal::ISpiTransport& link,
    std::uint32_t budget_words,
    bool fio_free
) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, A);

    const std::uint32_t left = budget_words - std::min(budget_words, rt_.round_words);
    const bool may_act = left >= kMaxRoundWords && (rt_.open.has_value() || fio_free);
    if (rt_.pull_out && may_act) {
        if (Loan l = chan_.reap()) {
            rt_.unanswered = 0;
            land_(std::move(l), link);
        } else if (rt_.open && ++rt_.unanswered >= kActLivenessRounds) {

            rt_.liveness_drops.add();
            rt_.order.owed = false;
            cut_();
        }
    }
    if (rt_.pull_out)
        return;

    while (Loan l = chan_.reap())
        rt_.stale.add();
    if (rt_.order.owed)
        start_(link);
}

void SaveUploadRelay::forget() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, A);
    do
        ++rt_.gen;
    while (rt_.gen == 0);
    cut_();
    rt_.order.owed = false;

    while (Loan l = chan_.reap())
        rt_.stale.add();
}

bool SaveUploadRelay::settled() const noexcept {
    return !rt_.pull_out && !rt_.order.owed;
}

bool SaveUploadRelay::drained() const noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, A);
    const Chan::Census c = chan_.census();
    return settled() && c.outbound == 0 && c.held_by_worker == 0;
}

std::uint32_t SaveUploadRelay::take_round_words() noexcept {
    return std::exchange(rt_.round_words, 0u);
}

void SaveUploadRelay::yield_to_reset() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, A);
    rt_.order.owed = false;
    if (!rt_.pull_out)
        return;
    cut_();
}

void SaveUploadRelay::cut_() noexcept {
    rt_.unanswered = 0;
    if (rt_.open) {
        rt_.open.reset();
        rt_.round_words += 2;
    }
    if (!rt_.pull_out)
        return;
    rt_.pull_out = false;
    do
        ++rt_.epoch;
    while (rt_.epoch == 0);
}

void SaveUploadRelay::end_pull_() noexcept {
    rt_.unanswered = 0;
    if (rt_.open) {
        rt_.open.reset();
        rt_.round_words += 2;
    }
    rt_.pull_out = false;
}

void SaveUploadRelay::start_(hal::ISpiTransport& link) noexcept {
    Loan l = chan_.acquire();
    if (!l)
        return;
    do
        ++rt_.epoch;
    while (rt_.epoch == 0);
    rt_.width = link.width();
    const Ask::Start s{
        .size = rt_.order.image.size,
        .index = rt_.order.image.index.v,
        .word_bytes = static_cast<std::uint8_t>(rt_.width == hal::Width::Word ? 2 : 1)
    };
    l->ask = infra::make<Ask>(s, head_());
    l->act = {};
    (void)l->rel.assign(rt_.order.rel.view());
    rt_.order.owed = false;
    rt_.pull_out = true;
    rt_.started.add();
    chan_.send(std::move(l));
}

void SaveUploadRelay::land_(Loan&& l, hal::ISpiTransport& link) noexcept {
    const Ask::Head h = l->ask.head;
    if (h.gen != rt_.gen || h.epoch != rt_.epoch) {
        rt_.stale.add();
        return;
    }
    if (!l.completed()) {
        rt_.stale.add();
        cut_();
        return;
    }
    const proto::SaveAct a = l->act;
    if (a.kind == proto::SaveAct::Kind::Done) {
        end_pull_();
        if (a.ok) {
            rt_.saved.add();
        } else {
            rt_.failed.add();
        }
        return;
    }
    if (!write_(a, l, link)) {
        cut_();
        return;
    }
    rt_.acts.add();
    l->ask = infra::make<Ask>(Ask::Resume{}, h);
    chan_.send(std::move(l));
}

bool SaveUploadRelay::write_(const proto::SaveAct& a, Loan& l, hal::ISpiTransport& link) noexcept {
    const std::uint32_t words = proto::save_act_words(a, rt_.width);
    switch (a.kind) {
        case proto::SaveAct::Kind::Open: {
            if (rt_.open)
                break;
            auto s = proto::UploadSession::begin(link, a.index);
            rt_.round_words += words;
            if (!s) {
                rt_.wire_faults.add();
                return false;
            }
            rt_.open.emplace(std::move(*s));
            return true;
        }
        case proto::SaveAct::Kind::Read: {

            if (!rt_.open || a.len == 0 || a.len > l->bytes.size() || words > kMaxRoundWords)
                break;
            const auto r = rt_.open->read(std::span<std::uint8_t>(l->bytes.data(), a.len));
            rt_.round_words += words;
            if (!r) {
                rt_.wire_faults.add();
                return false;
            }
            return true;
        }
        case proto::SaveAct::Kind::Close: {
            if (!rt_.open)
                break;
            const auto r = rt_.open->end();
            rt_.open.reset();
            rt_.round_words += words;
            if (!r) {
                rt_.wire_faults.add();
                return false;
            }
            return true;
        }
        case proto::SaveAct::Kind::Done:
            return true;
    }
    rt_.refused.add();
    return false;
}

void SaveUploadRelay::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, B);
    while (Job job = chan_.take()) {
        infra::dispatch<infra::AllRouted>(job->ask, *this, job);
        job.complete();
    }
}

void SaveUploadRelay::on(const Ask::Start& a, const Ask::Head& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, B);
    io_.serving = h;
    if (!io_.servant) {
        job->act = done(false);
        return;
    }
    const cores::SaveImage image{.index = proto::WideIoIndex{a.index}, .size = a.size};
    job->act = io_.servant->start(image, job->rel.view(), a.word_bytes);
}

void SaveUploadRelay::on(const Ask::Resume&, const Ask::Head& h, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, B);
    if (!io_.servant || h.gen != io_.serving.gen || h.epoch != io_.serving.epoch) {
        job->act = done(false);
        return;
    }
    job->act = io_.servant->resume(
        std::span<const std::uint8_t, proto::kSavePieceBytes>(job->bytes)
    );
}

void SaveUploadRelay::misrouted(const Ask&, Job& job) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(SaveUploadRelay, B);
    job->act = done(false);
}

}
