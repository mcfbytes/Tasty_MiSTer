// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/cyclone_v/cyclone_v_programmer.h"
#include "boards/cyclone_v/cyclone_v_windows.h"

#include "hal/board_windows.h"
#include "hal/boards_table.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

#include "infra/unique_fd.h"

namespace mister::boards {
namespace {

constexpr std::uint32_t kStageResetPhase = 1;
constexpr std::uint32_t kStageCfgPhase = 2;
constexpr std::uint32_t kStageConfigError = 3;
constexpr std::uint32_t kStageConfigDone = 4;
constexpr std::uint32_t kStageDclkEnter = 5;
constexpr std::uint32_t kStageInitPhase = 6;
constexpr std::uint32_t kStageDclkExit = 7;
constexpr std::uint32_t kStageUserMode = 8;

constexpr std::uint32_t detail_of(std::uint32_t stage, std::uint32_t observed) noexcept {
    return stage | (observed << 8);
}

constexpr std::uint32_t observed_of(std::uint32_t mode, std::uint32_t ext) noexcept {
    return (mode & FpgaMgr::kStatModeMask) | ((ext & 0xFu) << 4);
}

std::span<std::byte> bytes_of(const os::MmioRegion& m) noexcept {
    return std::span<std::byte>(reinterpret_cast<std::byte*>(const_cast<void*>(m.base())),
                                m.length());
}

}  // namespace

CycloneVProgrammer::CycloneVProgrammer(hal::RegisterWindow<FpgaMgr> mgr,
                                       hal::RegisterWindow<FpgaMgrData> data,
                                       std::optional<hal::FpgaMemory> handoff)
    : mgr_(std::move(mgr)), data_(std::move(data)), handoff_(std::move(handoff)) {}

CycloneVProgrammer CycloneVProgrammer::over(hal::RegisterWindow<FpgaMgr> mgr,
                                            hal::RegisterWindow<FpgaMgrData> data,
                                            hal::FpgaMemory handoff) {
    return CycloneVProgrammer(std::move(mgr), std::move(data), std::move(handoff));
}

Ex<CycloneVProgrammer> CycloneVProgrammer::open(hal::BoardWindows& windows) {

    auto mgr = windows.take<FpgaMgr>(cyclone_v::id(cyclone_v::Window::FpgaMgr));
    if (!mgr) return std::unexpected(mgr.error());
    auto data = windows.take<FpgaMgrData>(cyclone_v::id(cyclone_v::Window::FpgaMgrData));
    if (!data) return std::unexpected(data.error());
    auto hand = hal::FpgaMemory::map(kHandoffPage);
    if (!hand) return std::unexpected(hand.error());
    CycloneVProgrammer p(std::move(mgr->regs), std::move(data->regs), std::move(*hand));
    p.claims_ = {std::move(mgr->claim), std::move(data->claim)};
    return p;
}

Ex<CycloneVProgrammer> CycloneVProgrammer::open_at(const char* manager_dir) {
    if (manager_dir == nullptr) {
        return std::unexpected(
            Error{Errc::dt_missing, ERR_SITE(), static_cast<std::uint32_t>(EFAULT)});
    }

    const int fd = ::open(manager_dir, O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0) {
        const int e = errno;
        return std::unexpected(
            Error{(e == ENOENT || e == ENOTDIR) ? Errc::dt_missing : Errc::uio_open, ERR_SITE(),
                  static_cast<std::uint32_t>(e)});
    }
    UniqueFd owner(fd);

    auto mgr_pages = os::MmioRegion::map_fd(owner.get(), kFpgaMgrBase, FpgaMgr::kSize);
    if (!mgr_pages) return std::unexpected(mgr_pages.error());
    auto data_pages = os::MmioRegion::map_fd(owner.get(), kFpgaMgrDataBase, FpgaMgrData::kSize);
    if (!data_pages) return std::unexpected(data_pages.error());
    auto hand_pages = os::MmioRegion::map_fd(owner.get(), kHandoffPage.phys, kHandoffPage.len);
    if (!hand_pages) return std::unexpected(hand_pages.error());

    CycloneVProgrammer loader(
        hal::RegisterWindow<FpgaMgr>::borrow(mgr_pages->base(), mgr_pages->length()),
        hal::RegisterWindow<FpgaMgrData>::borrow(data_pages->base(), data_pages->length()),
        hal::FpgaMemory::borrow(bytes_of(*hand_pages), kHandoffPage));
    loader.mgr_pages_ = std::move(*mgr_pages);
    loader.data_pages_ = std::move(*data_pages);
    loader.handoff_pages_ = std::move(*hand_pages);
    return loader;
}

void CycloneVProgrammer::ctrl_update(std::uint32_t clear, std::uint32_t set) {
    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_);
    mgr_.write(FpgaMgr::Reg::Ctrl, (mgr_.read(FpgaMgr::Reg::Ctrl) & ~clear) | set);
}

void CycloneVProgrammer::set_cd_ratio(std::uint32_t ratio) {
    ctrl_update(FpgaMgr::kCtrlCdRatioMask, (ratio & 0x3u) << FpgaMgr::kCtrlCdRatioLsb);
}

std::uint32_t CycloneVProgrammer::mode() {
    return mgr_.read(FpgaMgr::Reg::Stat) & FpgaMgr::kStatModeMask;
}

std::uint32_t CycloneVProgrammer::ext_nibble() {
    return mgr_.read(FpgaMgr::Reg::GpioExtPorta) & 0xFu;
}

void CycloneVProgrammer::dclk_arm_(std::uint32_t count) {
    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_);

    if (mgr_.read(FpgaMgr::Reg::DclkStat) != 0u) {
        mgr_.write(FpgaMgr::Reg::DclkStat, FpgaMgr::kDclkStatDone);
    }
    mgr_.write(FpgaMgr::Reg::DclkCnt, count);
}

bool CycloneVProgrammer::dclk_done_() {
    if (mgr_.read(FpgaMgr::Reg::DclkStat) == 0u) return false;
    mgr_.write(FpgaMgr::Reg::DclkStat, FpgaMgr::kDclkStatDone);
    return true;
}

bool CycloneVProgrammer::rung_done_() {
    switch (step_) {
        case StepPc::Idle:
            return true;
        case StepPc::ResetWait:
            return mode() == FpgaMgr::kModeResetPhase;
        case StepPc::CfgWait:
            return mode() == FpgaMgr::kModeCfgPhase;
        case StepPc::DclkEnterWait:
        case StepPc::DclkExitWait:
            return dclk_done_();
        case StepPc::InitWait: {

            const std::uint32_t m = mode();
            return m == FpgaMgr::kModeInitPhase || m == FpgaMgr::kModeUserMode;
        }
        case StepPc::UserWait:
            return mode() == FpgaMgr::kModeUserMode;
    }
    return true;
}

CycloneVProgrammer::Awaited CycloneVProgrammer::await_(std::uint32_t polls) {
    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_);
    while (polls != 0u) {
        --polls;
        if (rung_done_()) return {true, polls};
    }
    return {false, 0u};
}

Ex<bool> CycloneVProgrammer::program_begin() {

    data_txn_.reset();
    tail_seen_ = false;
    step_ = StepPc::Idle;

    const std::uint32_t msel =
        (mgr_.read(FpgaMgr::Reg::Stat) & FpgaMgr::kStatMselMask) >> FpgaMgr::kStatMselLsb;

    if ((msel & 0x8u) != 0u) {
        ctrl_update(0, FpgaMgr::kCtrlCfgWdth);
        if ((msel & 0x3u) == 0x0u) {
            set_cd_ratio(FpgaMgr::kCdRatio1);
        } else if ((msel & 0x3u) == 0x1u) {
            set_cd_ratio(FpgaMgr::kCdRatio4);
        } else if ((msel & 0x3u) == 0x2u) {
            set_cd_ratio(FpgaMgr::kCdRatio8);
        }
    } else {
        ctrl_update(FpgaMgr::kCtrlCfgWdth, 0);
        if ((msel & 0x3u) == 0x0u) {
            set_cd_ratio(FpgaMgr::kCdRatio1);
        } else if ((msel & 0x3u) == 0x1u) {
            set_cd_ratio(FpgaMgr::kCdRatio2);
        } else if ((msel & 0x3u) == 0x2u) {
            set_cd_ratio(FpgaMgr::kCdRatio4);
        }
    }

    ctrl_update(FpgaMgr::kCtrlNce, 0);
    ctrl_update(0, FpgaMgr::kCtrlEn);
    ctrl_update(0, FpgaMgr::kCtrlNConfigPull);
    step_ = StepPc::ResetWait;
    return program_step();
}

void CycloneVProgrammer::program_write(std::span<const std::byte> image) {

    const std::byte* src = image.data();
    const std::size_t loops32 = image.size() / 32u;
    const std::size_t rem = image.size() % 32u;
    const std::size_t loops4 = rem / 4u;
    const std::size_t tail = rem % 4u;

    for (std::size_t g = 0; g < loops32; ++g) {
        for (std::size_t k = 0; k < FpgaMgrData::kBurstWords; ++k) {
            std::uint32_t w = 0;
            std::memcpy(&w, src, sizeof w);
            src += sizeof w;
            data_.write(static_cast<FpgaMgrData::Reg>(static_cast<std::uint32_t>(k * 4u)), w);
        }
    }
    for (std::size_t n = 0; n < loops4; ++n) {
        std::uint32_t w = 0;
        std::memcpy(&w, src, sizeof w);
        src += sizeof w;
        data_.write(FpgaMgrData::Reg::Data, w);
    }
    if (tail != 0) {
        std::uint32_t w = 0;
        std::memcpy(&w, src, tail);
        data_.write(FpgaMgrData::Reg::Data, w);
    }
}

Ex<void> CycloneVProgrammer::poll_config_done() {

    if (data_txn_.has_value()) {
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 1});
    }
    constexpr std::uint32_t kMask = FpgaMgr::kMonNs | FpgaMgr::kMonCd;

    std::uint32_t reg = 0;
    std::uint32_t i = 0;
    {
        hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_);
        for (; i < FpgaMgr::kTimeoutCount; ++i) {
            reg = mgr_.read(FpgaMgr::Reg::GpioExtPorta);
            if ((reg & kMask) == 0u) break;
            if ((reg & kMask) != 0u) break;
        }
    }

    if ((reg & kMask) == 0u) {
        return std::unexpected(Error{Errc::core_load, ERR_SITE(),
                                     detail_of(kStageConfigError, observed_of(mode(), reg))});
    }
    if (i == FpgaMgr::kTimeoutCount) {
        return std::unexpected(Error{Errc::timeout, ERR_SITE(),
                                     detail_of(kStageConfigDone, observed_of(mode(), reg))});
    }

    ctrl_update(FpgaMgr::kCtrlAxiCfgEn, 0);
    return {};
}

Ex<bool> CycloneVProgrammer::program_step() {

    Awaited a{true, FpgaMgr::kPollsPerPass};
    for (;;) {
        switch (step_) {
            case StepPc::Idle:
                return true;
            case StepPc::ResetWait:
                a = await_(a.left);
                if (!a.hit) return false;
                ctrl_update(FpgaMgr::kCtrlNConfigPull, 0);
                step_ = StepPc::CfgWait;
                break;
            case StepPc::CfgWait:
                a = await_(a.left);
                if (!a.hit) return false;
                {

                    hal::RegisterWindow<FpgaMgr>::Transaction t(mgr_);
                    mgr_.write(FpgaMgr::Reg::GpioPortaEoi, FpgaMgr::kEoiAll);
                }
                ctrl_update(0, FpgaMgr::kCtrlAxiCfgEn);
                data_txn_.emplace(data_);
                step_ = StepPc::Idle;
                return true;
            case StepPc::DclkEnterWait:
                a = await_(a.left);
                if (!a.hit) return false;
                step_ = StepPc::InitWait;
                break;
            case StepPc::InitWait:
                a = await_(a.left);
                if (!a.hit) return false;
                dclk_arm_(FpgaMgr::kDclkExitInit);
                step_ = StepPc::DclkExitWait;
                break;
            case StepPc::DclkExitWait:
                a = await_(a.left);
                if (!a.hit) return false;
                step_ = StepPc::UserWait;
                break;
            case StepPc::UserWait:
                a = await_(a.left);
                if (!a.hit) return false;

                ctrl_update(FpgaMgr::kCtrlEn, 0);
                step_ = StepPc::Idle;
                return true;
        }
    }
}

std::uint32_t CycloneVProgrammer::pending_stage() noexcept {
    switch (step_) {
        case StepPc::Idle:
            return 0;
        case StepPc::ResetWait:
            return detail_of(kStageResetPhase, mode());
        case StepPc::CfgWait:
            return detail_of(kStageCfgPhase, mode());
        case StepPc::DclkEnterWait:
            return detail_of(kStageDclkEnter, observed_of(mode(), ext_nibble()));
        case StepPc::InitWait:
            return detail_of(kStageInitPhase, observed_of(mode(), ext_nibble()));
        case StepPc::DclkExitWait:
            return detail_of(kStageDclkExit, 0);
        case StepPc::UserWait:
            return detail_of(kStageUserMode, mode());
    }
    return 0;
}

CycloneVProgrammer::CycloneVProgrammer(CycloneVProgrammer&& o) noexcept
    : mgr_pages_(std::move(o.mgr_pages_)), data_pages_(std::move(o.data_pages_)),
      handoff_pages_(std::move(o.handoff_pages_)), mgr_(std::move(o.mgr_)),
      data_(std::move(o.data_)), handoff_(std::move(o.handoff_)), claims_(std::move(o.claims_)) {
    if (o.data_txn_.has_value() || o.step_ != StepPc::Idle) {
        fatal(Error{Errc::bridge_state, ERR_SITE(), 0}, "CycloneVProgrammer: move mid-program");
    }
}

Ex<void> CycloneVProgrammer::program_chunk(std::span<const std::byte> chunk) {
    if (!data_txn_.has_value()) {
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    }
    if (chunk.empty()) return {};

    const auto misalign =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(chunk.data()) & 0x3u);
    if (misalign != 0) {
        data_txn_.reset();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), misalign});
    }

    if (tail_seen_) {
        data_txn_.reset();
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(chunk.size())});
    }
    program_write(chunk);
    tail_seen_ = (chunk.size() % 32u) != 0u;
    return {};
}

Ex<bool> CycloneVProgrammer::program_end() {
    if (!data_txn_.has_value()) {
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    }
    data_txn_.reset();
    tail_seen_ = false;
    if (auto r = poll_config_done(); !r) return std::unexpected(r.error());
    dclk_arm_(FpgaMgr::kDclkEnterInit);
    step_ = StepPc::DclkEnterWait;
    return program_step();
}

bool CycloneVProgrammer::programmed() const {
    if ((mgr_.read(FpgaMgr::Reg::GpioExtPorta) & FpgaMgr::kMonId) == 0u) {
        return false;
    }

    if ((mgr_.read(FpgaMgr::Reg::GpioExtPorta) & FpgaMgr::kMonId) == 0u) {
        return false;
    }
    return (mgr_.read(FpgaMgr::Reg::Stat) & FpgaMgr::kStatModeMask) == FpgaMgr::kModeUserMode;
}

std::byte* CycloneVProgrammer::handoff_base() {
    if (!handoff_.has_value()) return nullptr;
    return handoff_->view(0, kHandoffPage.len).data();
}

Ex<hal::HandoffPage> CycloneVProgrammer::handoff() {
    const std::byte* page = handoff_base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    const auto* raw = reinterpret_cast<const std::uint8_t*>(page);

    hal::HandoffPage out{};
    out.env_valid = raw[0] == kHandoffEnvCookie[0] && raw[1] == kHandoffEnvCookie[1] &&
                    raw[2] == kHandoffEnvCookie[2] && raw[3] == kHandoffEnvCookie[3];

    const std::uint8_t* sd = raw + kHandoffSdramOffset;
    if (sd[0] == kHandoffSdramCookie[0] && sd[1] == kHandoffSdramCookie[1]) {
        out.sdram_valid = true;
        out.sdram_cfg = static_cast<std::uint16_t>(kHandoffSdramPresent |
                                                   (static_cast<std::uint32_t>(sd[2]) << 8) |
                                                   static_cast<std::uint32_t>(sd[3]));
    }

    const std::uint8_t* al = raw + kHandoffAltCfgOffset;
    if (al[0] == kHandoffAltCfgCookie[0] && al[1] == kHandoffAltCfgCookie[1] &&
        al[2] == kHandoffAltCfgCookie[2]) {
        out.altcfg_valid = true;
        out.altcfg = al[3];
    }

    std::memcpy(&out.reboot_flag, raw + kHandoffRebootOffset, sizeof out.reboot_flag);
    return out;
}

Ex<void> CycloneVProgrammer::write_env(std::string_view core_name, std::span<const std::byte> cfg) {
    std::byte* page = handoff_base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }

    static constexpr char kCoreEq[] = {'c', 'o', 'r', 'e', '=', '"'};
    const std::size_t header = sizeof kHandoffEnvCookie + sizeof kCoreEq + core_name.size() + 2u;

    if (header > kHandoffEnvLimit || cfg.size() > kHandoffEnvLimit - header) {
        const std::size_t needed = header + cfg.size();
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(needed)});
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
    if (!cfg.empty()) std::memcpy(page + at, cfg.data(), cfg.size());
    return {};
}

Ex<void> CycloneVProgrammer::write_sdram_cfg(std::uint16_t cfg) {
    std::byte* page = handoff_base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    auto* cell = reinterpret_cast<std::uint8_t*>(page) + kHandoffSdramOffset;
    cell[0] = kHandoffSdramCookie[0];
    cell[1] = kHandoffSdramCookie[1];
    cell[2] = static_cast<std::uint8_t>(cfg >> 8);
    cell[3] = static_cast<std::uint8_t>(cfg & 0xFFu);
    return {};
}

Ex<void> CycloneVProgrammer::write_altcfg(std::uint8_t alt) {
    std::byte* page = handoff_base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    auto* cell = reinterpret_cast<std::uint8_t*>(page) + kHandoffAltCfgOffset;
    cell[0] = kHandoffAltCfgCookie[0];
    cell[1] = kHandoffAltCfgCookie[1];
    cell[2] = kHandoffAltCfgCookie[2];
    cell[3] = alt;
    return {};
}

Ex<void> CycloneVProgrammer::write_reboot_flag(bool warm) {
    std::byte* page = handoff_base();
    if (page == nullptr) {
        return std::unexpected(Error{Errc::mmap_failed, ERR_SITE(), os::low32(kHandoffPage.phys)});
    }
    const std::uint32_t flag = warm ? kHandoffRebootWarm : 0u;
    std::memcpy(page + kHandoffRebootOffset, &flag, sizeof flag);
    return {};
}

static_assert(hal::board_by_id(hal::BoardId::De10Nano).program.quantum.value() == 32u &&
              hal::board_by_id(hal::BoardId::De10Nano).program.align.value() == 4u);

}  // namespace mister::boards
