// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/durable_write_service.h"

#include <cstring>

#include "app/durable_write.h"

namespace mister::app {
namespace {

template <class JobT>
void perform_write(const svc::Vfs& vfs, JobT& job) noexcept {
    const auto r =
        durable_write(vfs, job->rel.view(), std::span<const std::byte>{job->data, job->len});
    job->ok = r ? 1u : 0u;
    job.complete();
}
}  // namespace

bool DurableWriteService::arm(std::string_view rel, std::span<const std::byte> bytes,
                              WriteKind kind) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "DurableWriteService::arm off T-RT");

    if (kind == WriteKind::Save) return arm_save_(rel, bytes);
    if (rel.empty() || bytes.size() > kWriteLaneBytes) {
        refusals_.add(1);
        return false;
    }
    auto loan = chan_.acquire();
    if (!loan) {
        refusals_.add(1);
        return false;
    }
    const std::uint8_t idx = chan_.index_of(loan);
    if (!loan->rel.assign(rel)) {
        refusals_.add(1);
        return false;
    }
    loan->len = static_cast<std::uint32_t>(bytes.size());
    loan->kind = kind;
    loan->ok = 0;
    if (!bytes.empty()) std::memcpy(loan->data, bytes.data(), bytes.size());

    Shadow& sh = shadow_[idx];
    (void)sh.rel.assign(rel);
    sh.len = loan->len;
    if (!bytes.empty()) std::memcpy(sh.data, bytes.data(), bytes.size());
    sh.seq = ++seq_;
    sh.live = true;

    chan_.send(std::move(loan));
    return true;
}

bool DurableWriteService::arm_save_(std::string_view rel,
                                    std::span<const std::byte> bytes) noexcept {
    if (rel.empty() || bytes.size() > kSaveLaneBytes) {
        refusals_.add(1);
        return false;
    }
    auto loan = save_chan_.acquire();
    if (!loan) {
        refusals_.add(1);
        return false;
    }
    if (!loan->rel.assign(rel)) {
        refusals_.add(1);
        return false;
    }
    loan->len = static_cast<std::uint32_t>(bytes.size());
    loan->kind = WriteKind::Save;
    loan->ok = 0;
    if (!bytes.empty()) std::memcpy(loan->data, bytes.data(), bytes.size());
    ++saves_armed_;
    save_chan_.send(std::move(loan));
    return true;
}

std::optional<DurableWriteService::WriteDone> DurableWriteService::reap() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "DurableWriteService::reap off T-RT");
    if (auto done = chan_.reap()) {
        shadow_[chan_.index_of(done)].live = false;
        const bool ok = done.completed() && done->ok != 0;
        if (ok) {
            writes_.add(1);
        } else {
            failures_.add(1);
        }
        return WriteDone{done->kind, ok};
    }
    auto done = save_chan_.reap();
    if (!done) return std::nullopt;
    if (saves_armed_ != 0) --saves_armed_;
    const bool ok = done.completed() && done->ok != 0;
    if (ok) {
        writes_.add(1);
    } else {
        failures_.add(1);
    }
    return WriteDone{done->kind, ok};
}

std::span<const std::byte> DurableWriteService::peek(std::string_view rel) const noexcept {

    const Shadow* best = nullptr;
    for (const Shadow& sh : shadow_) {
        if (!sh.live || sh.rel.view() != rel) continue;
        if (best == nullptr || sh.seq > best->seq) best = &sh;
    }
    if (best == nullptr) return {};
    return std::span<const std::byte>{best->data, best->len};
}

std::size_t DurableWriteService::in_flight() const noexcept {
    std::size_t n = saves_armed_;
    for (const Shadow& sh : shadow_)
        if (sh.live) ++n;
    return n;
}

void DurableWriteService::serve() noexcept {
    if (auto job = chan_.take()) {
        perform_write(*vfs_, job);
        return;
    }
    if (auto job = save_chan_.take()) perform_write(*vfs_, job);
}

bool DurableWriteService::idle() const noexcept {
    return chan_.outbound() == 0 && save_chan_.outbound() == 0;
}

void DurableWriteService::drain_on_caller() noexcept {

    const SeatScope stands_in_for_io{SeatTag::Io};
    for (std::size_t i = 0; i < kWriteDepth; ++i) {
        auto job = chan_.take();
        if (!job) break;
        perform_write(*vfs_, job);
    }
    for (std::size_t i = 0; i < kSaveDepth; ++i) {
        auto job = save_chan_.take();
        if (!job) break;
        perform_write(*vfs_, job);
    }
}

}  // namespace mister::app
