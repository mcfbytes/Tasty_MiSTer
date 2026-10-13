// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_builder.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "app/pcm_ring_feeder.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/generic_core.h"
#include "cores/generic_profile.h"
#include "cores/host_services.h"
#include "cores/registry.h"

namespace mister::app {

void CoreDeleter::operator()(cores::Core* p) const noexcept { delete p; }

CoreBuilder::CoreBuilder(CoreBay& bay, const BayReleasesCell& releases, const CoreTerms& terms,
                         infra::OptRef<hal::IDoorbellSource> src) noexcept
    : bay_(bay), releases_(releases), terms_(terms),
      doorbells_(src ? static_cast<hal::IDoorbellSource&>(*src) : uio_doorbells_) {}

CoreBuilder::~CoreBuilder() {

    for (auto& lease : held_)
        (void)lease.release();
}

proto::LinkOp::AdoptCore CoreBuilder::build(const Ask& ask) {
    using Built = proto::LinkOp::Built;

    reap();
    if (ask.name.empty()) return {.outcome = Built::NoName};
    const cores::LoadHint hint =
        ask.manifest.path.empty() ? cores::LoadHint::None : cores::LoadHint::XmlManifest;
    const auto row = cores::find_core(ask.name, hint);

    if (!terms_.vfs) return {.outcome = row ? Built::NoStorage : Built::RegistryMiss};
    CoreBay::Lease lease = bay_.acquire();
    if (!lease) {
        ++bay_full_;
        publish_counts_();
        return {.outcome = Built::Refused, .err = Errc::would_block};
    }
    BuiltCore& slot = *lease;

    const cores::CoreProfile& profile = row ? *(*row)->profile : cores::kGenericProfile;

    open_doorbells_(slot, ask.irqs);
    const cores::HostServices services{
        *terms_.vfs,
        terms_.link,
        terms_.clock,
        terms_.prefetch ? &*terms_.prefetch : nullptr,
        &terms_.bulk,
        grant_windows(slot, profile),
        terms_.discs ? &*terms_.discs : &terms_.unbound_discs,
        &terms_.fio_queue,
        terms_.mailbox ? &*terms_.mailbox : nullptr,
        terms_.save_copy,
    };
    const cores::CoreGrant grant{services, ask.manifest};
    slot.core = row ? (*row)->make(profile, grant) : cores::make_generic(profile, grant);
    const std::uint8_t at = bay_.index_of(&slot);

    do
        ++gen_;
    while (gen_ == 0 || gen_ == levels_.released_gen[at]);
    slot.gen = gen_;
    ++builds_;
    publish_counts_();
    held_[at] = std::move(lease);
    return {.slot = at, .outcome = Built::Core, .gen = gen_};
}

void CoreBuilder::forget(const proto::LinkOp::AdoptCore& op) noexcept {
    if (op.outcome != proto::LinkOp::Built::Core || op.slot >= held_.size()) return;
    if (held_[op.slot] && held_[op.slot]->gen == op.gen) free_(op.slot);
}

void CoreBuilder::reap() noexcept {
    BayReleases levels{};
    if (!released_.take_if_changed(releases_, levels)) return;
    levels_ = levels;
    for (std::size_t i = 0; i < held_.size(); ++i) {
        if (held_[i] && held_[i]->gen == levels.released_gen[i])
            free_(static_cast<std::uint8_t>(i));
    }
}

void CoreBuilder::drop_windows(const ParkReceipt&) noexcept {
    for (CoreBay::Lease& l : held_) {
        if (!l) continue;
        for (auto& w : l->windows)
            w.reset();
        l->fed_ring.reset();

        l->lines = DoorbellLines{};
    }
}

void CoreBuilder::open_doorbells_(BuiltCore& into,
                                  std::span<const proto::IrqBinding> irqs) noexcept {
    uio_doorbells_.set_lw_window(terms_.lw_window);
    uio_doorbells_.set_line_space(terms_.doorbell_nodes);
    open_doorbells(doorbells_, terms_.doorbells, irqs, into.lines);
}

void CoreBuilder::open_doorbells(hal::IDoorbellSource& src, const hal::DoorbellPolicy& policy,
                                 std::span<const proto::IrqBinding> irqs,
                                 DoorbellLines& into) noexcept {
    into = DoorbellLines{};
    const std::size_t n = std::min(irqs.size(), kMaxDoorbellRows);
    into.count = n;
    for (std::size_t i = 0; i < n; ++i) {
        const proto::IrqBinding& b = irqs[i];
        DoorbellLines::Line& line = into.rows[i];
        line.binding = b;

        if (!policy.line_in_pool(static_cast<std::uint32_t>(b.line))) {
            line.gate = Errc::slot_range;
            continue;
        }

        if (b.cause_base.v != 0 && !policy.cause_in_aperture(b.cause_base, hal::CauseReg::kSize)) {
            line.gate = Errc::bad_format;
            continue;
        }
        auto uio = src.open_line(b.line);
        if (!uio) {
            line.why = uio.error().code;
            continue;
        }
        if (b.cause_base.v != 0) {
            auto w = src.map_cause(b.cause_base);
            if (!w) {
                line.why = w.error().code;
                continue;
            }
            line.cause.emplace(std::move(*w));
        }
        line.handle = std::move(*uio);
    }
}

cores::CoreWindowGrant CoreBuilder::grant_windows(BuiltCore& into,
                                                  const cores::CoreProfile& profile) noexcept {
    cores::CoreWindowGrant grant{};

    const std::size_t n = std::min(profile.windows.size(), cores::kMaxCoreWindows);
    for (std::size_t i = 0; i < n; ++i) {
        const cores::CoreWindowDecl& row = profile.windows[i];
        grant.names[i] = row.region.name;

        if (row.feeds_pcm && (!terms_.feeder || into.fed_ring.has_value())) {
            ++window_refusals_;
            continue;
        }
        auto mem = CoreWindow::map_row(terms_.aperture, row.region);
        if (!mem) {
            ++window_refusals_;
            continue;
        }
        if (row.feeds_pcm) {
            into.fed_ring.emplace(std::move(*mem));
            grant.windows[i] = &*terms_.feeder;
            continue;
        }
        into.windows[i].emplace(std::move(*mem));
        grant.windows[i] = &*into.windows[i];
    }
    return grant;
}

std::uint32_t CoreBuilder::leased() const noexcept {
    return static_cast<std::uint32_t>(std::ranges::count_if(
        held_, [](const CoreBay::Lease& l) noexcept { return l != nullptr; }));
}

void CoreBuilder::free_(std::uint8_t slot) noexcept {
    BuiltCore& b = *held_[slot];
    b.core.reset();
    for (auto& w : b.windows)
        w.reset();
    b.fed_ring.reset();
    b.lines = DoorbellLines{};
    b.gen = 0;
    held_[slot].reset();
    ++frees_;
    publish_counts_();
}

void CoreBuilder::publish_counts_() noexcept {
    counts_cell_.publish(BayCounts{.builds = builds_,
                                   .bay_full = bay_full_,
                                   .frees = frees_,
                                   .window_refusals = window_refusals_});
}

}  // namespace mister::app
