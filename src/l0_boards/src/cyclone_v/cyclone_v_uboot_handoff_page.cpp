// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/cyclone_v/cyclone_v_uboot_handoff_page.h"

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
    out.env = kEnvCell.present(raw);

    if (kSdramCell.present(raw)) {
        out.sdram = hal::SdramCfg{
            infra::load_be<std::uint16_t>(kSdramCell.value_of(raw).first<sizeof(std::uint16_t)>())
        };
    }

    if (kAltCfgCell.present(raw)) {
        out.altcfg = kAltCfgCell.value_of(raw)[0];
    }

    const auto reboot_word = infra::load_native<std::uint32_t>(
        std::span<const std::byte, sizeof(std::uint32_t)>(
            page + kHandoffRebootOffset,
            sizeof(std::uint32_t)
        )
    );
    out.reboot = hal::RebootFlag{reboot_word};
    return out;
}

Ex<void> UbootHandoffPage::write_env(std::string_view core_name, std::span<const std::byte> cfg) {
    TASTY_SEAT_BODY(UbootHandoffPage);
    std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }

    static constexpr char kCoreEq[] = {'c', 'o', 'r', 'e', '=', '"'};
    const std::size_t header = kEnvCell.cookie.size()
        + sizeof kCoreEq
        + core_name.size()
        + kEnvTrailer.size();

    if (header > kHandoffEnvLimit || cfg.size() > kHandoffEnvLimit - header) {
        const std::size_t needed = header + cfg.size();
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(needed)}
        );
    }

    std::memset(page, 0, kHandoffEnvLimit);

    auto bytes = infra::byte_cast<std::uint8_t>(std::span<std::byte>(page, kHandoffEnvLimit));
    kEnvCell.stamp(bytes);
    std::size_t at = kEnvCell.cookie.size();
    std::memcpy(page + at, kCoreEq, sizeof kCoreEq);
    at += sizeof kCoreEq;
    if (!core_name.empty()) {
        std::memcpy(page + at, core_name.data(), core_name.size());
        at += core_name.size();
    }
    std::memcpy(page + at, kEnvTrailer.data(), kEnvTrailer.size());
    at += kEnvTrailer.size();
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
    auto bytes = infra::byte_cast<std::uint8_t>(std::span<std::byte>(page, kHandoffPage.len));
    kSdramCell.stamp(bytes);
    infra::store_be<std::uint16_t>(kSdramCell.value_of(bytes).first<sizeof(std::uint16_t)>(), cfg);
    return {};
}

Ex<void> UbootHandoffPage::write_altcfg(std::uint8_t alt) {
    TASTY_SEAT_BODY(UbootHandoffPage);
    std::byte* page = base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    auto bytes = infra::byte_cast<std::uint8_t>(std::span<std::byte>(page, kHandoffPage.len));
    kAltCfgCell.stamp(bytes);
    kAltCfgCell.value_of(bytes)[0] = alt;
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
