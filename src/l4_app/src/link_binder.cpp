// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_binder.h"

#include "app/pcm_ring_feeder.h"

#include <ctime>
#include <optional>
#include <utility>

#include "cores/core_support.h"

namespace mister::app {
namespace {

std::uint64_t log_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

}  // namespace

LinkBinder::LinkBinder(hal::ISpiTransport& link, const Wiring& w) noexcept
    : fio_queue_(link), spi_sink_(link, &fio_queue_), exec_(w.exec), state_(w.state),
      notifiers_(w.exec), log_lane_(w.log_lane) {}

proto::IImageSink& LinkBinder::grant_bulk() noexcept { return progress_sink_; }

void LinkBinder::release_feeder() noexcept {
    if (feeder_granted_ && feeder_ != nullptr) feeder_->detach();
    feeder_granted_ = false;
}

bool LinkBinder::adopt_fed_ring(hal::FpgaMemory&& ring) noexcept {

    if (feeder_ == nullptr || !feeder_->adopt_ring(std::move(ring))) {
        ++fed_refusals_;
        return false;
    }
    feeder_granted_ = true;
    return true;
}

Ex<void> LinkBinder::bind_census(std::span<const reactor::LinkDecoderDecl> rows,
                                 cores::Core* owner) {

    reactor::ICoreToken* const prev = state_.owner;
    state_.owner = rows.empty() ? nullptr : owner;
    auto r = exec_.bind(rows, state_);
    if (!r) state_.owner = prev;
    return r;
}

void LinkBinder::bind_doorbells_(std::span<const proto::IrqBinding> irqs, std::uint32_t declared,
                                 const DoorbellLines& lines) noexcept {
    notifiers_.release();
    doorbells_declared_ = declared;
    bound_classes_ = reactor::CauseSet{};
    for (std::size_t i = 0; i < irqs.size(); ++i) {

        if (i >= lines.count) {
            fall_back(irqs[i], Errc::dt_missing);
            continue;
        }
        bind_one_doorbell(irqs[i], lines.rows[i]);
    }
}

Ex<void> LinkBinder::bind_session(std::span<const reactor::LinkDecoderDecl> rows,
                                  cores::Core* owner, std::span<const proto::IrqBinding> irqs,
                                  std::uint32_t declared, const DoorbellLines& lines) {
    for (const LinkRow& row : kLinkRows) {
        if (row.phase != BindPhase::Bind) continue;
        switch (row.kind) {
            case LinkKind::ImageSink:
                break;
            case LinkKind::CoreWindow:
                break;
            case LinkKind::CensusRows: {
                if (auto r = bind_census(rows, owner); !r) return r;
                break;
            }
            case LinkKind::Doorbells:
                bind_doorbells_(irqs, declared, lines);
                break;
            case LinkKind::BlockSlots:
                break;
            case LinkKind::kCount:
                break;
        }
    }
    return {};
}

void LinkBinder::bind_one_doorbell(const proto::IrqBinding& b,
                                   const DoorbellLines::Line& line) noexcept {

    if (!(line.binding == b)) return fall_back(b, Errc::stale);

    if (line.gate) return fall_back(b, *line.gate);

    if (bound_classes_.has(b.klass)) return fall_back(b, Errc::negotiation);

    if (line.why) return fall_back(b, *line.why);

    auto slot = notifiers_.bind_doorbell(line.handle.borrow(), b.klass,
                                         infra::opt_ref_of(line.cause ? &*line.cause : nullptr));
    if (!slot) return fall_back(b, slot.error().code);
    bound_classes_ |= reactor::CauseSet{b.klass};
    (void)log_lane_.push(LogRec::DoorbellBound{.cause_base = b.cause_base.v,
                                               .klass = static_cast<std::uint8_t>(b.klass),
                                               .line = static_cast<std::uint8_t>(b.line)},
                         log_now_ns());
}

void LinkBinder::fall_back(const proto::IrqBinding& b, Errc why) noexcept {
    ++fallbacks_;
    (void)log_lane_.push(LogRec::DoorbellPolling{.why = why,
                                                 .klass = static_cast<std::uint8_t>(b.klass),
                                                 .line = static_cast<std::uint8_t>(b.line)},
                         log_now_ns());
}

DoorbellStats LinkBinder::doorbell_stats() const noexcept {
    DoorbellStats d{};
    d.declared = doorbells_declared_;
    d.bound = static_cast<std::uint32_t>(notifiers_.size());
    d.refusals = fallbacks_;
    d.fallbacks = exec_.doorbell_fallbacks();
    d.retirements = exec_.doorbell_retirements();
    d.release_failures = notifiers_.release_failures();
    return d;
}

bool LinkBinder::bridge_windows_live() const noexcept {

    if (notifiers_.size() != 0) return true;

    if (feeder_ != nullptr && feeder_->attached()) return true;
    return feeder_granted_;
}

Ex<void> LinkBinder::unbind_session() {
    auto r = bind_census({}, nullptr);
    notifiers_.release();
    doorbells_declared_ = 0;
    bound_classes_ = reactor::CauseSet{};
    return r;
}

}  // namespace mister::app
