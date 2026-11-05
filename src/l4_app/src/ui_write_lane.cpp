// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ui_write_lane.h"

#include <cstring>
#include <utility>

#include "app/durable_write.h"
#include "app/verdict_sinks.h"

namespace mister::app {

Ex<void> UiWriteLane::arm(
    std::string_view rel,
    std::span<const std::byte> bytes,
    VerdictSink sink
) noexcept {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "UiWriteLane::arm off T-UI");
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
    if (!loan->rel.assign(rel)) {
        ++ui_.refusals;
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    loan->len = static_cast<std::uint32_t>(bytes.size());
    loan->ok = false;
    if (!bytes.empty())
        std::memcpy(loan->data, bytes.data(), bytes.size());

    Shadow& sh = ui_.shadow[chan_.index_of(loan)];
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
        Shadow& sh = ui_.shadow[chan_.index_of(done)];
        sh.live = false;
        const bool ok = done.completed() && done->ok;
        if (ok) {
            ++ui_.writes;
        } else {
            ++ui_.failures;
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
    const auto r = durable_write(
        *vfs_,
        job->rel.view(),
        std::span<const std::byte>{job->data, job->len}
    );
    job->ok = r.has_value();
    job.complete();
}

bool UiWriteLane::idle() const noexcept {
    return chan_.outbound() == 0;
}

}
