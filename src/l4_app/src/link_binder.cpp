// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/link_binder.h"

#include "app/pcm_ring_feeder.h"

#include <algorithm>
#include <ctime>
#include <optional>
#include <utility>

#include "cores/core_support.h"
#include "cores/core_profile.h"

namespace mister::app {
namespace {

std::uint64_t log_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

}  // namespace

LinkBinder::LinkBinder(hal::ISpiTransport& link, hal::IDoorbellSource& src,
                       const Wiring& w) noexcept
    : fio_queue_(link), spi_sink_(link, &fio_queue_), aperture_(w.fpga_mem), doorbells_(src),
      policy_(w.doorbells), exec_(w.exec), state_(w.state), notifiers_(w.exec),
      log_lane_(w.log_lane) {
    uio_doorbells_.set_lw_window(w.lw_window);
    uio_doorbells_.set_line_space(w.doorbell_nodes);
}

proto::IImageSink& LinkBinder::grant_bulk() noexcept { return progress_sink_; }

void LinkBinder::release_windows() noexcept {
    for (auto& w : windows_)
        w.reset();
    if (feeder_granted_ && feeder_ != nullptr) feeder_->detach();
    feeder_granted_ = false;
}

bool LinkBinder::grant_fed_(hal::FpgaMemory ring) noexcept {
    if (feeder_ == nullptr || !feeder_->attach_ring(std::move(ring))) return false;
    feeder_granted_ = true;
    return true;
}

cores::CoreWindowGrant LinkBinder::grant_windows(const cores::CoreProfile& profile) noexcept {
    release_windows();
    cores::CoreWindowGrant grant{};

    const std::size_t n = std::min(profile.windows.size(), cores::kMaxCoreWindows);
    for (std::size_t i = 0; i < n; ++i) {
        const cores::CoreWindowDecl& row = profile.windows[i];
        grant.names[i] = row.region.name;
        auto mem = CoreWindow::map_row(aperture_, row.region);
        if (!mem) {
            ++window_refusals_;
            continue;
        }
        if (row.feeds_pcm) {

            if (!grant_fed_(std::move(*mem))) {
                ++window_refusals_;
                continue;
            }
            grant.windows[i] = feeder_;
            continue;
        }
        windows_[i].emplace(std::move(*mem));
        grant.windows[i] = &*windows_[i];
    }
    return grant;
}

Ex<void> LinkBinder::bind_census(std::span<const reactor::LinkDecoderDecl> rows,
                                 cores::Core* owner) {

    reactor::ICoreToken* const prev = state_.owner;
    state_.owner = rows.empty() ? nullptr : owner;
    auto r = exec_.bind(rows, state_);
    if (!r) state_.owner = prev;
    return r;
}

void LinkBinder::bind_doorbells_(std::span<const proto::IrqBinding> irqs,
                                 std::uint32_t declared) noexcept {
    notifiers_.release();
    doorbells_declared_ = declared;
    bound_classes_ = reactor::CauseSet{};
    for (const proto::IrqBinding& b : irqs)
        bind_one_doorbell(b);
}

Ex<void> LinkBinder::bind_session(std::span<const reactor::LinkDecoderDecl> rows,
                                  cores::Core* owner, std::span<const proto::IrqBinding> irqs,
                                  std::uint32_t declared) {
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
                bind_doorbells_(irqs, declared);
                break;
            case LinkKind::BlockSlots:
                break;
            case LinkKind::kCount:
                break;
        }
    }
    return {};
}

void LinkBinder::bind_one_doorbell(const proto::IrqBinding& b) noexcept {

    if (!policy_.line_in_pool(static_cast<std::uint32_t>(b.line))) {
        return fall_back(b, Errc::slot_range);
    }

    if (b.cause_base.v != 0 && !policy_.cause_in_aperture(b.cause_base, hal::CauseReg::kSize)) {
        return fall_back(b, Errc::bad_format);
    }

    if (bound_classes_.has(b.klass)) return fall_back(b, Errc::negotiation);

    auto uio = doorbells_.open_line(b.line);
    if (!uio) return fall_back(b, uio.error().code);

    std::optional<hal::RegisterWindow<hal::CauseReg>> window;
    if (b.cause_base.v != 0) {
        auto w = doorbells_.map_cause(b.cause_base);
        if (!w) return fall_back(b, w.error().code);
        window.emplace(std::move(*w));
    }
    auto slot = notifiers_.bind_doorbell(std::move(*uio), b.klass, std::move(window));
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
    return d;
}

bool LinkBinder::bridge_windows_live() const noexcept {
    if (notifiers_.size() != 0) return true;

    if (feeder_ != nullptr && feeder_->attached()) return true;
    if (feeder_granted_) return true;
    for (const auto& w : windows_) {
        if (w.has_value()) return true;
    }
    return false;
}

Ex<void> LinkBinder::unbind_session() {
    auto r = bind_census({}, nullptr);
    notifiers_.release();
    doorbells_declared_ = 0;
    bound_classes_ = reactor::CauseSet{};
    return r;
}

}  // namespace mister::app
