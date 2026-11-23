// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "svc/nic_host_seed.h"
#include "svc/nic_link.h"
#include "svc/nic_link_plan.h"
#include "svc/nic_mac.h"
#include "svc/nic_port.h"

namespace mister::svc {

class NicHostPort final : public INicPort {
    TASTY_SEAT_RESIDENT(Nic);

public:
    NicHostPort() = default;
    NicHostPort(const NicHostPort&) = delete;
    NicHostPort& operator=(const NicHostPort&) = delete;

    [[nodiscard]] bool supports(NicLink link) override;
    [[nodiscard]] std::optional<MacAddress> iface_mac(std::string_view iface) override;
    void prepare(const NicLinkPlan& plan, std::optional<MacAddress> carried) override;
    [[nodiscard]] Ex<void> open(const NicLinkPlan& plan) override;
    [[nodiscard]] MacAddress host_mac() const noexcept override;
    void filter(std::optional<MacAddress> accept) override;
    void send(std::span<const std::byte> frame) override;
    [[nodiscard]] std::span<std::byte> receive(std::span<std::byte> into) override;
    void close() noexcept override;
    [[nodiscard]] NicHostSeed host_seed() override;

private:
    enum class Probe : std::uint8_t { Unknown, Absent, Present };

    [[nodiscard]] Ex<void> open_tap_(std::string_view iface);
    [[nodiscard]] Ex<void> open_packet_(std::string_view iface, bool promiscuous);
    void dhcpcd_deny_(std::string_view iface);

    [[nodiscard]] bool macvlan_create_(const NicLinkPlan& plan, const MacAddress& mac);

    infra::UniqueFd fd_{};
    bool tap_ = false;
    MacAddress host_mac_{};
    std::string created_macvlan_{};
    Probe tap_probe_ = Probe::Unknown;
    Probe macvlan_probe_ = Probe::Unknown;
};

}
