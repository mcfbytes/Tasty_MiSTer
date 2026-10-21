// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "proto/axi_ddr_decl.h"
#include "proto/axi_kind.h"
#include "proto/axi_lw_decl.h"
#include "proto/axi_unkinded_decl.h"
#include "proto/capability.h"
#include "infra/seat.h"
#include "reactor/notifier.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "os/types.h"
#include "proto/irq_binding.h"
#include "proto/save_state_decl.h"

namespace mister::proto {

class ConfStrCaps {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] std::size_t try_ss(
        std::string_view f1,
        std::size_t pos,
        hal::PhysRegion aperture
    );
    [[nodiscard]] std::size_t try_uart(std::string_view f1, std::size_t pos);
    [[nodiscard]] std::size_t try_midi(std::string_view f1, std::size_t pos);
    [[nodiscard]] Ex<std::size_t> try_irq(std::string_view f1, std::size_t pos);
    [[nodiscard]] Ex<std::size_t> try_axi(std::string_view f1, std::size_t pos);

    void carry_unknown(std::string_view run);

    const std::vector<Capability>& capabilities() const noexcept { return caps_; }

    const std::vector<IrqBinding>& irq_bindings() const noexcept { return irq_; }

    const std::vector<AxiLwDecl>& lw_decls() const noexcept { return axi_lw_; }

    const std::vector<AxiDdrDecl>& ddr_decls() const noexcept { return axi_ddr_; }

    const std::vector<AxiUnkindedDecl>& unkinded_decls() const noexcept { return axi_unkinded_; }

    std::uint32_t axi_unknown_kind_count() const noexcept { return axi_unknown_kinds_; }

    const std::optional<SaveStateDecl>& savestate() const noexcept { return ss_; }

    std::optional<os::UioLine> irq_line_for(reactor::Cause klass) const;

private:
    std::vector<Capability> caps_;
    std::vector<IrqBinding> irq_;
    std::vector<AxiLwDecl> axi_lw_;
    std::vector<AxiDdrDecl> axi_ddr_;
    std::vector<AxiUnkindedDecl> axi_unkinded_;
    std::uint32_t axi_unknown_kinds_ = 0;
    std::optional<SaveStateDecl> ss_;
};

}
