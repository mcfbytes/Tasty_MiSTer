// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "hal/fpga_memory.h"
#include "hal/hps_ddr_region.h"
#include "infra/byte_offset.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "svc/nic_slot_word.h"
#include "svc/nic_window.h"
#include "svc/nic_window_source.h"

namespace mister::svc {

class NicMappedWindow final : public INicWindowSource, private INicWindow {
    TASTY_SEAT_RESIDENT(Nic);

public:
    explicit NicMappedWindow(const hal::HpsDdrRegion& page) noexcept : page_(page) {}

    NicMappedWindow(const NicMappedWindow&) = delete;
    NicMappedWindow& operator=(const NicMappedWindow&) = delete;

    [[nodiscard]] Ex<void> open() override;
    [[nodiscard]] INicWindow& window() noexcept override;
    void close() noexcept override;

private:
    [[nodiscard]] std::byte load(infra::ByteOffset at) const noexcept override;
    void store(infra::ByteOffset at, std::byte b) noexcept override;
    [[nodiscard]] NicSlotWord load_slot(infra::ByteOffset at) const noexcept override;
    void store_slot(infra::ByteOffset at, NicSlotWord w) noexcept override;

    hal::HpsDdrRegion page_;
    std::optional<hal::FpgaMemory> map_;
};

}
