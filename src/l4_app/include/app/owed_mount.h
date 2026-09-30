// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "app/link_rx_channel.h"
#include "app/mount_status_cell.h"
#include "app/path_text.h"
#include "cores/mount_status.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::app {

class OwedMount {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Cell = MountStatusCell;

    OwedMount() noexcept = default;
    OwedMount(const OwedMount&) = delete;
    OwedMount& operator=(const OwedMount&) = delete;
    OwedMount(OwedMount&&) = delete;

    OwedMount& operator=(OwedMount&& other) noexcept {
        if (this != &other) {
            strand();
            adopt_(other);
        }
        return *this;
    }
    ~OwedMount() { strand(); }

    void bind(Cell& cell, LinkRxChannel* rx) noexcept {
        strand();
        cell_ = &cell;
        rx_ = rx;
    }

    [[nodiscard]] bool owed() const noexcept { return gen_ != 0; }
    [[nodiscard]] std::uint32_t generation() const noexcept { return gen_; }
    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }
    [[nodiscard]] bool parked() const noexcept { return parked_; }
    void mark_parked() noexcept { parked_ = true; }
    [[nodiscard]] std::int64_t quiesce_deadline_ns() const noexcept { return quiesce_deadline_ns_; }

    void arm(std::uint32_t generation, const cores::MountStatus& on_strand, std::string_view path,
             bool parked, std::int64_t quiesce_deadline_ns) noexcept {
        if (gen_ != 0 && gen_ != generation) strand();
        gen_ = generation;
        strand_body_ = on_strand;
        (void)path_.assign(path);
        parked_ = parked;
        quiesce_deadline_ns_ = quiesce_deadline_ns;
    }

    void answer(cores::MountVerdict v, const cores::MountStatus& payload) noexcept {
        if (gen_ == 0) return;
        publish_(std::exchange(gen_, 0u), v, payload);
        forget_();
    }

    void strand() noexcept {
        if (gen_ == 0) return;
        publish_(std::exchange(gen_, 0u), cores::MountVerdict::Failed, strand_body_);
        forget_();
    }

private:
    void publish_(std::uint32_t generation, cores::MountVerdict v,
                  cores::MountStatus body) noexcept {
        set_verdict(body, v);
        body.generation = generation;
        if (cell_ != nullptr) cell_->publish(body);
        if (rx_ != nullptr) rx_->request_wake();
    }
    void forget_() noexcept {
        path_.clear();
        parked_ = false;
        quiesce_deadline_ns_ = 0;
    }
    void adopt_(OwedMount& other) noexcept {
        cell_ = other.cell_;
        rx_ = other.rx_;
        strand_body_ = other.strand_body_;
        path_ = other.path_;
        parked_ = other.parked_;
        quiesce_deadline_ns_ = other.quiesce_deadline_ns_;
        gen_ = std::exchange(other.gen_, 0u);
    }

    Cell* cell_ = nullptr;
    LinkRxChannel* rx_ = nullptr;
    cores::MountStatus strand_body_{};
    FixedStr<kPathMax, StrFit::Clip> path_{};
    std::int64_t quiesce_deadline_ns_ = 0;
    std::uint32_t gen_ = 0;
    bool parked_ = false;
};

}  // namespace mister::app
