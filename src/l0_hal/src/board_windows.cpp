// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/board_windows.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace mister::hal {

Ex<BoardWindows> BoardWindows::open(const BoardProfile& profile, FallbackNotice notice) {
    if (!windows_well_formed(profile.windows)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    BoardWindows w{profile};
    for (const WindowDecl& d : profile.windows) {
        if (d.id == profile.lw_window) continue;
        auto m = detail::map_named(d.uio_name, d.region.phys, d.region.len);
        if (!m) return std::unexpected(m.error());

        if (!m->via_uio && notice == FallbackNotice::Announce) {
            std::fprintf(stderr,
                         "mister: window \"%s\" (0x%08llx) mapped via /dev/mem — no UIO node "
                         "named \"%s\"\n",
                         d.region.name, static_cast<unsigned long long>(d.region.phys.v),
                         d.uio_name);
        }
        const auto slot = static_cast<std::size_t>(d.id);
        const detail::MappedRegs& page = w.pages_[slot].emplace(std::move(*m));
        const std::size_t len =
            page.pages.length() >= page.lead ? page.pages.length() - page.lead : 0;
        w.lends_[slot] = Lend{detail::regs_advance(page.pages.base(), page.lead), len};
    }
    return w;
}

Ex<BoardWindows> BoardWindows::attach(const BoardProfile& profile,
                                      std::span<const WindowImage> images) {
    if (!windows_well_formed(profile.windows)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    BoardWindows w{profile};
    for (const WindowImage& img : images) {
        const auto slot = static_cast<std::size_t>(img.id);
        if (slot >= profile.windows.size() || img.id == profile.lw_window || img.base == nullptr ||
            w.lends_[slot].base != nullptr) {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(slot)});
        }

        w.lends_[slot] = Lend{img.base, std::min(img.len, profile.windows[slot].region.len)};
    }
    return w;
}

Ex<volatile void*> BoardWindows::lead_of(WindowId id, std::size_t need) const {
    const auto slot = static_cast<std::size_t>(id);
    if (slot >= profile_->windows.size() || lends_[slot].base == nullptr) {
        return std::unexpected(
            Error{Errc::dt_missing, ERR_SITE(), static_cast<std::uint32_t>(slot)});
    }
    if (need == 0 || lends_[slot].len < need) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(need)});
    }
    return lends_[slot].base;
}

Ex<Mailbox> BoardWindows::take_mailbox() {
    auto gpo = mailbox_gpo();
    if (!gpo) return std::unexpected(gpo.error());
    auto gpi = mailbox_gpi();
    if (!gpi) return std::unexpected(gpi.error());
    const auto timing = LinkTimingValues::resolve(profile_->timing, ERR_SITE());
    if (!timing) return std::unexpected(timing.error());
    auto claim = mailbox_claims_->claim(MailboxId::SpiMaster);
    if (!claim) return std::unexpected(claim.error());
    return Mailbox{MailboxRegs{*gpo, *gpi}, *timing, std::move(*claim)};
}

Ex<volatile std::uint32_t*> BoardWindows::mailbox_gpo() const {
    const auto fact = profile_->mailbox_gpo_offset.resolve(
        ERR_SITE(), static_cast<std::uint32_t>(FactField::MailboxGpoOffset));
    if (!fact) return std::unexpected(fact.error());
    const std::uint32_t off = *fact;
    auto base =
        lead_of(profile_->mailbox_window, static_cast<std::size_t>(off) + sizeof(std::uint32_t));
    if (!base) return std::unexpected(base.error());
    return reinterpret_cast<volatile std::uint32_t*>(detail::regs_advance(*base, off));
}

Ex<const volatile std::uint32_t*> BoardWindows::mailbox_gpi() const {
    const auto fact = profile_->mailbox_gpi_offset.resolve(
        ERR_SITE(), static_cast<std::uint32_t>(FactField::MailboxGpiOffset));
    if (!fact) return std::unexpected(fact.error());
    const std::uint32_t off = *fact;
    auto base =
        lead_of(profile_->mailbox_window, static_cast<std::size_t>(off) + sizeof(std::uint32_t));
    if (!base) return std::unexpected(base.error());
    return reinterpret_cast<const volatile std::uint32_t*>(detail::regs_advance(*base, off));
}

}  // namespace mister::hal
