// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "cores/load_ask.h"
#include "cores/load_plan.h"
#include "cores/loader.h"
#include "cores/transfer_row.h"
#include "cores/types.h"
#include "cores/x86/uma_row.h"
#include "infra/seat.h"
#include "proto/status_word.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::x86 {

class X86BootLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr CoreWindowSlot kGuestMemory{0};

    static constexpr auto kShadow = std::to_array<UmaRow>({
        {.guest_at = WindowOffset{0xA0000},
         .bytes = proto::FileSize{0x60000},
         .fill = std::byte{0xFF}},
        {.guest_at = WindowOffset{0xA0000}, .bytes = proto::FileSize{0x20000}},
        {.guest_at = WindowOffset{0xCE000}, .bytes = proto::FileSize{0x2000}},
        {.guest_at = WindowOffset{0xC0000}, .rom = "boot1.rom"},
        {.guest_at = WindowOffset{0xF0000}, .rom = "boot0.rom"},
    });

    X86BootLoader(const svc::Vfs& vfs, std::string_view home) noexcept : vfs_(vfs), home_(home) {}

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;
    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow> window) override;

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(X86BootLoader);
        return {};
    }

private:
    const svc::Vfs& vfs_;
    std::string_view home_;

    std::array<std::string, kShadow.size()> paths_{};
};

}
