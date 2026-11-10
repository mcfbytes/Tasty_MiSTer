// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/cyclone_v/cyclone_v_programmer.h"
#include "boards/cyclone_v/cyclone_v_windows.h"

#include "hal/board_windows.h"
#include "hal/taken_window.h"
#include "hal/boards_table.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <span>
#include <utility>

#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/unique_fd.h"
#include "os/map_length.h"

namespace mister::boards::cyclone_v {
namespace {

enum : unsigned {
    kDetailObservedShift = 8,
    kObservedExtShift = 4,
    kExtPortaMask = 0xF,
};

constexpr std::size_t kBurstBytes = FpgaMgrData::kBurstWords * sizeof(std::uint32_t);

enum class LoadStage : std::uint32_t {
    ResetPhase = 1,
    CfgPhase = 2,
    ConfigError = 3,
    ConfigDone = 4,
    DclkEnter = 5,
    InitPhase = 6,
    DclkExit = 7,
    UserMode = 8,
};

constexpr std::uint32_t detail_of(LoadStage stage, std::uint32_t observed) noexcept {
    return static_cast<std::uint32_t>(stage) | (observed << kDetailObservedShift);
}

constexpr std::uint32_t observed_of(FpgaMgrMode mode, std::uint32_t ext) noexcept {
    return (to_bits(mode) & FpgaMgr::kStatModeMask) | ((ext & kExtPortaMask) << kObservedExtShift);
}

constexpr FpgaMgrMode mode_of(std::uint32_t stat) noexcept {
    switch (stat & FpgaMgr::kStatModeMask) {
        case to_bits(FpgaMgrMode::Off):
            return FpgaMgrMode::Off;
        case to_bits(FpgaMgrMode::Reset):
            return FpgaMgrMode::Reset;
        case to_bits(FpgaMgrMode::Cfg):
            return FpgaMgrMode::Cfg;
        case to_bits(FpgaMgrMode::Init):
            return FpgaMgrMode::Init;
        case to_bits(FpgaMgrMode::User):
            return FpgaMgrMode::User;
    }
    return FpgaMgrMode::Unknown;
}

}

Programmer::Programmer(
    hal::TakenWindow<FpgaMgr> mgr,
    hal::TakenWindow<FpgaMgrData> data,
    std::optional<hal::FpgaMemory> handoff
)
    : mgr_(std::move(mgr)),
      data_(std::move(data)),
      page_(std::move(handoff)) {}

Programmer Programmer::over(
    hal::RegisterWindow<FpgaMgr> mgr,
    hal::RegisterWindow<FpgaMgrData> data,
    hal::FpgaMemory handoff
) {
    return Programmer(
        hal::TakenWindow<FpgaMgr>{std::move(mgr), hal::WindowClaim{}},
        hal::TakenWindow<FpgaMgrData>{std::move(data), hal::WindowClaim{}},
        std::move(handoff)
    );
}

Ex<Programmer> Programmer::open(hal::BoardWindows& windows) {

    auto mgr = windows.take<FpgaMgr>(cyclone_v::id(cyclone_v::Window::FpgaMgr));
    if (!mgr)
        return std::unexpected(mgr.error());
    auto data = windows.take<FpgaMgrData>(cyclone_v::id(cyclone_v::Window::FpgaMgrData));
    if (!data)
        return std::unexpected(data.error());
    auto hand = hal::FpgaMemory::map(kHandoffPage);
    if (!hand)
        return std::unexpected(hand.error());
    return Programmer(std::move(*mgr), std::move(*data), std::move(*hand));
}

Ex<Programmer> Programmer::open_at(const char* manager_dir) {
    if (manager_dir == nullptr) {
        return std::unexpected(
            Error{Errc::dt_missing, ERR_SITE(), static_cast<std::uint32_t>(EFAULT)}
        );
    }

    const int fd = ::open(manager_dir, O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
        const int e = errno;
        return std::unexpected(
            Error{
                (e == ENOENT || e == ENOTDIR) ? Errc::dt_missing : Errc::uio_open,
                ERR_SITE(),
                static_cast<std::uint32_t>(e)
            }
        );
    }
    UniqueFd owner(fd);

    using MapOffset = os::MmioRegion::MapOffset;
    auto mgr_pages = os::MmioRegion::map_fd(
        owner.get(),
        MapOffset::of(kFpgaMgrBase),
        os::MapLength{FpgaMgr::kSize}
    );
    if (!mgr_pages)
        return std::unexpected(mgr_pages.error());
    auto data_pages = os::MmioRegion::map_fd(
        owner.get(),
        MapOffset::of(kFpgaMgrDataBase),
        os::MapLength{FpgaMgrData::kSize}
    );
    if (!data_pages)
        return std::unexpected(data_pages.error());
    auto hand_pages = os::MmioRegion::map_fd(
        owner.get(),
        MapOffset::of(kHandoffPage.phys),
        os::MapLength{kHandoffPage.len}
    );
    if (!hand_pages)
        return std::unexpected(hand_pages.error());

    Programmer loader = Programmer::over(
        hal::RegisterWindow<FpgaMgr>::borrow(mgr_pages->base(), mgr_pages->length()),
        hal::RegisterWindow<FpgaMgrData>::borrow(data_pages->base(), data_pages->length()),
        hal::FpgaMemory::borrow(hand_pages->plain_bytes(), kHandoffPage)
    );
    loader.mgr_pages_ = std::move(*mgr_pages);
    loader.data_pages_ = std::move(*data_pages);
    loader.handoff_pages_ = std::move(*hand_pages);
    return loader;
}

void Programmer::ctrl_update(FpgaCtrl clear, FpgaCtrl set) {
    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_.regs);
    const FpgaCtrl next = FpgaCtrl{mgr_.regs.read(FpgaMgr::Reg::Ctrl)}.without(clear).with(set);
    mgr_.regs.write(FpgaMgr::Reg::Ctrl, next.v);
}

void Programmer::set_cd_ratio(CdRatio ratio) {
    ctrl_update(kCtrlCdRatioMask, cd_ratio_field(ratio));
}

FpgaMgrMode Programmer::mode() const {
    return mode_of(mgr_.regs.read(FpgaMgr::Reg::Stat));
}

std::uint32_t Programmer::ext_nibble() {
    return mgr_.regs.read(FpgaMgr::Reg::GpioExtPorta) & kExtPortaMask;
}

void Programmer::dclk_arm_(std::uint32_t count) {
    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_.regs);

    if (mgr_.regs.read(FpgaMgr::Reg::DclkStat) != 0u) {
        mgr_.regs.write(FpgaMgr::Reg::DclkStat, FpgaMgr::kDclkStatDone);
    }
    mgr_.regs.write(FpgaMgr::Reg::DclkCnt, count);
}

bool Programmer::dclk_done_() {
    if (mgr_.regs.read(FpgaMgr::Reg::DclkStat) == 0u)
        return false;
    mgr_.regs.write(FpgaMgr::Reg::DclkStat, FpgaMgr::kDclkStatDone);
    return true;
}

bool Programmer::rung_done_() {
    switch (step_) {
        case StepPc::Idle:
            return true;
        case StepPc::ResetWait:
            return mode() == FpgaMgrMode::Reset;
        case StepPc::CfgWait:
            return mode() == FpgaMgrMode::Cfg;
        case StepPc::DclkEnterWait:
        case StepPc::DclkExitWait:
            return dclk_done_();
        case StepPc::InitWait: {

            const FpgaMgrMode m = mode();
            return m == FpgaMgrMode::Init || m == FpgaMgrMode::User;
        }
        case StepPc::UserWait:
            return mode() == FpgaMgrMode::User;
    }
    return true;
}

Programmer::Awaited Programmer::await_(std::uint32_t polls) {
    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_.regs);
    while (polls != 0u) {
        --polls;
        if (rung_done_())
            return {true, polls};
    }
    return {false, 0u};
}

Ex<bool> Programmer::program_begin() {
    TASTY_SEAT_BODY(Programmer);

    data_txn_.reset();
    tail_seen_ = false;
    step_ = StepPc::Idle;

    const std::uint32_t msel = (mgr_.regs.read(FpgaMgr::Reg::Stat) & FpgaMgr::kStatMselMask)
        >> FpgaMgr::kStatMselLsb;

    const bool wide = (msel & FpgaMgr::kMselWideMask) != 0u;
    ctrl_update(wide ? kCtrlNone : kCtrlCfgWdth, wide ? kCtrlCfgWdth : kCtrlNone);
    if (const auto ratio = cd_ratio_of(msel))
        set_cd_ratio(*ratio);

    ctrl_update(kCtrlNce, kCtrlNone);
    ctrl_update(kCtrlNone, kCtrlEn);
    ctrl_update(kCtrlNone, kCtrlNConfigPull);
    step_ = StepPc::ResetWait;
    return program_step();
}

void Programmer::program_write(std::span<const std::byte> image) {

    const std::byte* src = image.data();
    const std::size_t loops32 = image.size() / kBurstBytes;
    const std::size_t rem = image.size() % kBurstBytes;
    const std::size_t loops4 = rem / sizeof(std::uint32_t);
    const std::size_t tail = rem % sizeof(std::uint32_t);

    for (std::size_t g = 0; g < loops32; ++g) {
        for (std::size_t k = 0; k < FpgaMgrData::kBurstWords; ++k) {
            const auto w = infra::load_le<std::uint32_t>(
                std::span<const std::byte, sizeof(std::uint32_t)>(src, sizeof(std::uint32_t))
            );
            src += sizeof w;
            data_.regs.write(
                static_cast<FpgaMgrData::Reg>(
                    static_cast<std::uint32_t>(k * sizeof(std::uint32_t))
                ),
                w
            );
        }
    }
    for (std::size_t n = 0; n < loops4; ++n) {
        const auto w = infra::load_le<std::uint32_t>(
            std::span<const std::byte, sizeof(std::uint32_t)>(src, sizeof(std::uint32_t))
        );
        src += sizeof w;
        data_.regs.write(FpgaMgrData::Reg::Data, w);
    }
    if (tail != 0) {
        std::uint32_t w = 0;
        std::memcpy(&w, src, tail);
        data_.regs.write(FpgaMgrData::Reg::Data, w);
    }
}

Ex<void> Programmer::poll_config_done() {

    if (data_txn_.has_value()) {
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 1});
    }
    constexpr std::uint32_t kMask = FpgaMgr::kMonNs | FpgaMgr::kMonCd;

    std::uint32_t reg = 0;
    std::uint32_t i = 0;
    {
        hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_.regs);
        for (; i < FpgaMgr::kTimeoutCount; ++i) {
            reg = mgr_.regs.read(FpgaMgr::Reg::GpioExtPorta);
            if ((reg & kMask) == 0u)
                break;
            if ((reg & kMask) != 0u)
                break;
        }
    }

    if ((reg & kMask) == 0u) {
        return std::unexpected(
            Error{
                Errc::core_load,
                ERR_SITE(),
                detail_of(LoadStage::ConfigError, observed_of(mode(), reg))
            }
        );
    }
    if (i == FpgaMgr::kTimeoutCount) {
        return std::unexpected(
            Error{
                Errc::timeout,
                ERR_SITE(),
                detail_of(LoadStage::ConfigDone, observed_of(mode(), reg))
            }
        );
    }

    ctrl_update(kCtrlAxiCfgEn, kCtrlNone);
    return {};
}

Ex<bool> Programmer::program_step() {
    TASTY_SEAT_BODY(Programmer);

    Awaited a{true, FpgaMgr::kPollsPerPass};
    for (;;) {
        switch (step_) {
            case StepPc::Idle:
                return true;
            case StepPc::ResetWait:
                a = await_(a.left);
                if (!a.hit)
                    return false;
                ctrl_update(kCtrlNConfigPull, kCtrlNone);
                step_ = StepPc::CfgWait;
                break;
            case StepPc::CfgWait:
                a = await_(a.left);
                if (!a.hit)
                    return false;
                {

                    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_.regs);
                    mgr_.regs.write(FpgaMgr::Reg::GpioPortaEoi, FpgaMgr::kEoiAll);
                }
                ctrl_update(kCtrlNone, kCtrlAxiCfgEn);
                data_txn_.emplace(data_.regs);
                step_ = StepPc::Idle;
                return true;
            case StepPc::DclkEnterWait:
                a = await_(a.left);
                if (!a.hit)
                    return false;
                step_ = StepPc::InitWait;
                break;
            case StepPc::InitWait:
                a = await_(a.left);
                if (!a.hit)
                    return false;
                dclk_arm_(FpgaMgr::kDclkExitInit);
                step_ = StepPc::DclkExitWait;
                break;
            case StepPc::DclkExitWait:
                a = await_(a.left);
                if (!a.hit)
                    return false;
                step_ = StepPc::UserWait;
                break;
            case StepPc::UserWait:
                a = await_(a.left);
                if (!a.hit)
                    return false;

                ctrl_update(kCtrlEn, kCtrlNone);
                step_ = StepPc::Idle;
                return true;
        }
    }
}

std::uint32_t Programmer::pending_stage() noexcept {
    TASTY_SEAT_BODY(Programmer);
    switch (step_) {
        case StepPc::Idle:
            return 0;
        case StepPc::ResetWait:
            return detail_of(LoadStage::ResetPhase, to_bits(mode()));
        case StepPc::CfgWait:
            return detail_of(LoadStage::CfgPhase, to_bits(mode()));
        case StepPc::DclkEnterWait:
            return detail_of(LoadStage::DclkEnter, observed_of(mode(), ext_nibble()));
        case StepPc::InitWait:
            return detail_of(LoadStage::InitPhase, observed_of(mode(), ext_nibble()));
        case StepPc::DclkExitWait:
            return detail_of(LoadStage::DclkExit, 0);
        case StepPc::UserWait:
            return detail_of(LoadStage::UserMode, to_bits(mode()));
    }
    return 0;
}

Programmer::Programmer(Programmer&& o) noexcept
    : mgr_pages_(std::move(o.mgr_pages_)),
      data_pages_(std::move(o.data_pages_)),
      handoff_pages_(std::move(o.handoff_pages_)),
      mgr_(std::move(o.mgr_)),
      data_(std::move(o.data_)),
      page_(std::move(o.page_)) {
    if (o.data_txn_.has_value() || o.step_ != StepPc::Idle) {
        fatal(Error{Errc::bridge_state, ERR_SITE(), 0}, "Programmer: move mid-program");
    }
}

Ex<void> Programmer::program_chunk(std::span<const std::byte> chunk) {
    TASTY_SEAT_BODY(Programmer);
    if (!data_txn_.has_value()) {
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    }
    if (chunk.empty())
        return {};

    if (!infra::is_aligned(chunk.data(), hal::kWordAlign)) {
        data_txn_.reset();
        return std::unexpected(
            Error{
                Errc::bad_format,
                ERR_SITE(),
                static_cast<std::uint32_t>(infra::misalignment(chunk.data(), hal::kWordAlign))
            }
        );
    }

    if (tail_seen_) {
        data_txn_.reset();
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(chunk.size())}
        );
    }
    program_write(chunk);
    tail_seen_ = (chunk.size() % kBurstBytes) != 0u;
    return {};
}

Ex<bool> Programmer::program_end() {
    TASTY_SEAT_BODY(Programmer);
    if (!data_txn_.has_value()) {
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    }
    data_txn_.reset();
    tail_seen_ = false;
    if (auto r = poll_config_done(); !r)
        return std::unexpected(r.error());
    dclk_arm_(FpgaMgr::kDclkEnterInit);
    step_ = StepPc::DclkEnterWait;
    return program_step();
}

bool Programmer::programmed() const {
    TASTY_SEAT_BODY(Programmer);
    if ((mgr_.regs.read(FpgaMgr::Reg::GpioExtPorta) & FpgaMgr::kMonId) == 0u) {
        return false;
    }

    if ((mgr_.regs.read(FpgaMgr::Reg::GpioExtPorta) & FpgaMgr::kMonId) == 0u) {
        return false;
    }
    return mode() == FpgaMgrMode::User;
}

static_assert(
    hal::board_by_id(hal::BoardId::De10Nano).program.quantum.value() == hal::QuantumBytes{32u}
    && hal::board_by_id(hal::BoardId::De10Nano).program.align.value() == hal::AlignBytes{4u}
);

}
