// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/durable_write_service.h"

#include <cstring>

#include "app/durable_write.h"

namespace mister::app {
namespace {

template <class JobT>
void perform_write(const svc::Vfs& vfs, JobT& job) noexcept {
    const auto r = durable_write(
        vfs,
        job->rel.view(),
        std::span<const std::byte>{job->data, job->len}
    );
    job->ok = r ? 1u : 0u;
    job.complete();
}
}

bool DurableWriteService::arm(std::string_view rel, std::span<const std::byte> bytes) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "DurableWriteService::arm off T-RT");
    if (rel.empty() || bytes.size() > kWriteLaneBytes) {
        rt_.refusals.add(1);
        return false;
    }
    auto loan = chan_.acquire();
    if (!loan) {
        rt_.refusals.add(1);
        return false;
    }
    const std::uint8_t idx = chan_.index_of(loan);
    if (!loan->rel.assign(rel)) {
        rt_.refusals.add(1);
        return false;
    }
    loan->len = static_cast<std::uint32_t>(bytes.size());
    loan->ok = 0;
    if (!bytes.empty())
        std::memcpy(loan->data, bytes.data(), bytes.size());

    Shadow& sh = rt_.shadow[idx];
    (void)sh.rel.assign(rel);
    sh.len = loan->len;
    if (!bytes.empty())
        std::memcpy(sh.data, bytes.data(), bytes.size());
    sh.seq = ++rt_.seq;
    sh.live = true;

    chan_.send(std::move(loan));
    return true;
}

std::optional<DurableWriteService::WriteDone> DurableWriteService::reap() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "DurableWriteService::reap off T-RT");
    auto done = chan_.reap();
    if (!done)
        return std::nullopt;
    rt_.shadow[chan_.index_of(done)].live = false;
    const bool ok = done.completed() && done->ok != 0;
    if (ok) {
        rt_.writes.add(1);
    } else {
        rt_.failures.add(1);
    }
    return WriteDone{ok};
}

std::span<const std::byte> DurableWriteService::peek(std::string_view rel) const noexcept {

    const Shadow* best = nullptr;
    for (const Shadow& sh : rt_.shadow) {
        if (!sh.live || sh.rel.view() != rel)
            continue;
        if (best == nullptr || sh.seq > best->seq)
            best = &sh;
    }
    if (best == nullptr)
        return {};
    return std::span<const std::byte>{best->data, best->len};
}

std::size_t DurableWriteService::in_flight() const noexcept {
    std::size_t n = 0;
    for (const Shadow& sh : rt_.shadow)
        if (sh.live)
            ++n;
    return n;
}

void DurableWriteService::serve() noexcept {
    if (auto job = chan_.take())
        perform_write(*vfs_, job);
}

bool DurableWriteService::idle() const noexcept {
    return chan_.outbound() == 0;
}

void DurableWriteService::drain_on_caller() noexcept {

    const SeatScope stands_in_for_io{SeatTag::Io};
    for (std::size_t i = 0; i < kWriteDepth; ++i) {
        auto job = chan_.take();
        if (!job)
            break;
        perform_write(*vfs_, job);
    }
}

}
