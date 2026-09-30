// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/boot_handoff.h"

namespace mister::hal {

class IFpgaProgrammer {
public:
    virtual ~IFpgaProgrammer() = default;

    [[nodiscard]] virtual Ex<bool> program_begin() = 0;
    [[nodiscard]] virtual Ex<void> program_chunk(std::span<const std::byte> chunk) = 0;
    [[nodiscard]] virtual Ex<bool> program_end() = 0;

    [[nodiscard]] virtual Ex<bool> program_step() = 0;

    [[nodiscard]] virtual std::uint32_t pending_stage() noexcept { return 0; }

    static constexpr std::uint32_t kProgramStepBudget = 0x10000;

    [[nodiscard]] Ex<void> program(std::span<const std::byte> bitstream) {
        if (bitstream.empty()) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
        }
        const auto misalign =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bitstream.data()) & 0x3u);
        if (misalign != 0) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), misalign});
        }
        if (auto r = to_rest_(program_begin()); !r) return r;
        if (auto r = program_chunk(bitstream); !r) return r;
        return to_rest_(program_end());
    }

    [[nodiscard]] Ex<void> drive_to_rest(Ex<bool> started) { return to_rest_(started); }

    virtual bool programmed() const = 0;

    virtual IBootHandoff* boot_handoff() noexcept { return nullptr; }

protected:
    [[nodiscard]] Ex<void> to_rest_(Ex<bool> started) {
        if (!started) return std::unexpected(started.error());
        bool done = *started;
        for (std::uint32_t i = 0; !done && i < kProgramStepBudget; ++i) {
            auto s = program_step();
            if (!s) return std::unexpected(s.error());
            done = *s;
        }
        if (!done) return std::unexpected(Error{Errc::timeout, ERR_SITE(), pending_stage()});
        return {};
    }

    IFpgaProgrammer() = default;
    IFpgaProgrammer(const IFpgaProgrammer&) = default;
    IFpgaProgrammer& operator=(const IFpgaProgrammer&) = default;
};

}  // namespace mister::hal
