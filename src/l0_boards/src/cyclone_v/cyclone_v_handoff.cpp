// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/cyclone_v/cyclone_v_handoff.h"

#include <algorithm>
#include <cstring>

#include "infra/byte_view.h"
#include "infra/endian.h"

namespace mister::boards::cyclone_v {

std::byte* UbootHandoffPage::base() noexcept {
    if (!page_)
        return nullptr;
    return page_->view(0, kHandoffPage.len).data();
}

Ex<hal::HandoffPage> UbootHandoffPage::handoff() {
    TASTY_SEAT_BODY(UbootHandoffPage);
    const std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    const auto raw = infra::byte_cast<const std::uint8_t>(
        std::span<const std::byte>(page, kHandoffPage.len)
    );

    hal::HandoffPage out{};
    out.env_valid = std::equal(
        std::begin(kHandoffEnvCookie),
        std::end(kHandoffEnvCookie),
        raw.begin()
    );

    const auto sd = raw.subspan(kHandoffSdramOffset);
    if (sd[0] == kHandoffSdramCookie[0] && sd[1] == kHandoffSdramCookie[1]) {
        out.sdram_valid = true;
        out.sdram_cfg = static_cast<std::uint16_t>(
            kHandoffSdramPresent
            | infra::load_be<std::uint16_t>(
                sd.subspan<HandoffCell::kSdramValueAt, sizeof(std::uint16_t)>()
            )
        );
    }

    const auto al = raw.subspan(kHandoffAltCfgOffset);
    if (al[0] == kHandoffAltCfgCookie[0]
        && al[1] == kHandoffAltCfgCookie[1]
        && al[HandoffCell::kCookieTail] == kHandoffAltCfgCookie[HandoffCell::kCookieTail]) {
        out.altcfg_valid = true;
        out.altcfg = al[HandoffCell::kAltCfgValue];
    }

    out.reboot_flag = infra::load_native<std::uint32_t>(
        std::span<const std::byte, sizeof(std::uint32_t)>(
            page + kHandoffRebootOffset,
            sizeof(std::uint32_t)
        )
    );
    return out;
}

Ex<void> UbootHandoffPage::write_env(std::string_view core_name, std::span<const std::byte> cfg) {
    TASTY_SEAT_BODY(UbootHandoffPage);
    std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }

    static constexpr char kCoreEq[] = {'c', 'o', 'r', 'e', '=', '"'};
    const std::size_t header = sizeof kHandoffEnvCookie
        + sizeof kCoreEq
        + core_name.size()
        + HandoffCell::kEnvTrailerBytes;

    if (header > kHandoffEnvLimit || cfg.size() > kHandoffEnvLimit - header) {
        const std::size_t needed = header + cfg.size();
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(needed)}
        );
    }

    std::memset(page, 0, kHandoffEnvLimit);

    std::size_t at = 0;
    std::memcpy(page + at, kHandoffEnvCookie, sizeof kHandoffEnvCookie);
    at += sizeof kHandoffEnvCookie;
    std::memcpy(page + at, kCoreEq, sizeof kCoreEq);
    at += sizeof kCoreEq;
    if (!core_name.empty()) {
        std::memcpy(page + at, core_name.data(), core_name.size());
        at += core_name.size();
    }
    page[at++] = static_cast<std::byte>('"');
    page[at++] = static_cast<std::byte>('\n');
    if (!cfg.empty())
        std::memcpy(page + at, cfg.data(), cfg.size());
    return {};
}

Ex<void> UbootHandoffPage::write_sdram_cfg(std::uint16_t cfg) {
    TASTY_SEAT_BODY(UbootHandoffPage);
    std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    auto cell = infra::byte_cast<std::uint8_t>(std::span<std::byte>(page, kHandoffPage.len))
                    .subspan(kHandoffSdramOffset);
    cell[0] = kHandoffSdramCookie[0];
    cell[1] = kHandoffSdramCookie[1];
    infra::store_be<std::uint16_t>(
        cell.subspan<HandoffCell::kSdramValueAt, sizeof(std::uint16_t)>(),
        cfg
    );
    return {};
}

Ex<void> UbootHandoffPage::write_altcfg(std::uint8_t alt) {
    TASTY_SEAT_BODY(UbootHandoffPage);
    std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    auto cell = infra::byte_cast<std::uint8_t>(std::span<std::byte>(page, kHandoffPage.len))
                    .subspan(kHandoffAltCfgOffset);
    cell[0] = kHandoffAltCfgCookie[0];
    cell[1] = kHandoffAltCfgCookie[1];
    cell[HandoffCell::kCookieTail] = kHandoffAltCfgCookie[HandoffCell::kCookieTail];
    cell[HandoffCell::kAltCfgValue] = alt;
    return {};
}

Ex<void> UbootHandoffPage::write_reboot_flag(bool warm) {
    TASTY_SEAT_BODY(UbootHandoffPage);
    std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    const std::uint32_t flag = warm ? kHandoffRebootWarm : 0u;
    infra::store_native<std::uint32_t>(
        std::span<std::byte, sizeof(std::uint32_t)>(
            page + kHandoffRebootOffset,
            sizeof(std::uint32_t)
        ),
        flag
    );
    return {};
}

}
