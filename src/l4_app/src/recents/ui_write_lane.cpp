// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recents/ui_write_lane.h"

#include <cstring>
#include <utility>

#include "app/recents/verdict_sinks.h"
#include "infra/prelude.h"

namespace mister::app::recents {

Ex<void> UiWriteLane::arm(
    std::string_view rel,
    std::span<const std::byte> bytes,
    VerdictSink sink
) noexcept {
    return arm(Target{.rel = rel}, bytes, sink);
}

Ex<void> UiWriteLane::arm(
    Target target,
    std::span<const std::byte> bytes,
    VerdictSink sink
) noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "UiWriteLane::arm off T-UI");
    const std::string_view rel = target.rel;
    if (rel.empty() || bytes.size() > kBytes) {
        ++ui_.refusals;
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(bytes.size())}
        );
    }
    auto loan = chan_.acquire();
    if (!loan) {
        ++ui_.refusals;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    if (!loan->rel.assign(rel) || !loan->retire.assign(target.retire)) {
        ++ui_.refusals;
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    loan->len = static_cast<std::uint32_t>(bytes.size());
    loan->fault.reset();
    if (!bytes.empty())
        std::memcpy(loan->data, bytes.data(), bytes.size());

    Shadow& sh = ui_.shadow[*chan_.index_of(loan)];
    (void)sh.rel.assign(rel);
    sh.len = loan->len;
    sh.held = bytes.size() <= kShadowBytes;
    if (sh.held && !bytes.empty())
        std::memcpy(sh.data, bytes.data(), bytes.size());
    sh.seq = ++ui_.seq;
    sh.sink = sink;
    sh.live = true;

    chan_.send(std::move(loan));
    return {};
}

void UiWriteLane::tick(VerdictSinks sinks) noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "UiWriteLane::tick off T-UI");
    while (auto done = chan_.reap()) {
        Shadow& sh = ui_.shadow[*chan_.index_of(done)];
        sh.live = false;
        const bool ok = done.completed() && !done->fault;
        if (ok) {
            ++ui_.writes;
        } else {
            ++ui_.failures;
            if (done.completed()) {
                ui_.last_fault_ = done->fault;
            } else {
                ui_.last_fault_ = Error{Errc::cancelled, ERR_SITE(), 0};
            }
        }
        const VerdictSink named = std::exchange(sh.sink, VerdictSink::None);
        switch (named) {
            case VerdictSink::None:
                break;
            case VerdictSink::Played:
                if (sinks.played)
                    sinks.played->on_written(ok);
                break;
            case VerdictSink::Recents:
                if (sinks.recents)
                    sinks.recents->on_written(ok);
                break;
            case VerdictSink::Maps:
                if (sinks.maps)
                    sinks.maps->on_written(ok);
                break;
        }
    }
}

Ex<std::span<const std::byte>> UiWriteLane::peek(std::string_view rel) const noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "UiWriteLane::peek off T-UI");
    const Shadow* best = nullptr;
    for (const Shadow& sh : ui_.shadow) {
        if (!sh.live || sh.rel.view() != rel)
            continue;
        if (best == nullptr || sh.seq > best->seq)
            best = &sh;
    }
    if (best == nullptr)
        return std::span<const std::byte>{};

    if (!best->held)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), best->len});
    return std::span<const std::byte>{best->data, best->len};
}

std::size_t UiWriteLane::in_flight() const noexcept {
    std::size_t n = 0;
    for (const Shadow& sh : ui_.shadow)
        if (sh.live)
            ++n;
    return n;
}

void UiWriteLane::serve() noexcept {
    auto job = chan_.take();
    if (!job)
        return;

    if (!job->retire.empty()) {
        if (const auto gone = card_.remove(job->retire.view()); !gone) {
            job->fault = gone.error();
            job.complete();
            return;
        }
    }
    const auto r = card_.replace_file(
        job->rel.view(),
        std::span<const std::byte>{job->data, job->len}
    );
    if (!r)
        job->fault = r.error();
    job.complete();
}

bool UiWriteLane::idle() const noexcept {
    return chan_.outbound() == 0;
}

}
