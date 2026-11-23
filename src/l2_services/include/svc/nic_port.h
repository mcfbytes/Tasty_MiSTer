// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/nic_host_seed.h"
#include "svc/nic_link.h"
#include "svc/nic_link_plan.h"
#include "svc/nic_mac.h"

namespace mister::svc {

class INicPort {
public:
    virtual ~INicPort() = default;

    [[nodiscard]] virtual bool supports(NicLink link) = 0;

    [[nodiscard]] virtual std::optional<MacAddress> iface_mac(std::string_view iface) = 0;

    virtual void prepare(const NicLinkPlan& plan, std::optional<MacAddress> carried) = 0;

    [[nodiscard]] virtual Ex<void> open(const NicLinkPlan& plan) = 0;

    [[nodiscard]] virtual MacAddress host_mac() const noexcept = 0;

    virtual void filter(std::optional<MacAddress> accept) = 0;
    virtual void send(std::span<const std::byte> frame) = 0;

    [[nodiscard]] virtual std::span<std::byte> receive(std::span<std::byte> into) = 0;

    virtual void close() noexcept = 0;

    [[nodiscard]] virtual NicHostSeed host_seed() = 0;

protected:
    INicPort() = default;
    INicPort(const INicPort&) = default;
    INicPort& operator=(const INicPort&) = default;
};

}
