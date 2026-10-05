// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <pthread.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "rt_evidence.h"
#include "hal/thread_map.h"
#include "infra/error.h"
#include "infra/wake_flag.h"
#include "diag_main.h"
#include "diag_sampler.h"
#include "ui_main.h"
#include "infra/seat.h"

namespace mister::xthread {
struct RtStats;
}
namespace mister::app {
class CmdFifo;
class EventQueue;
class InputBuild;
class InputMain;
class PcmMain;
class RtMain;
class MglPump;
class VideoPump;
class BtPump;
class CaptureMain;
class EncodeMain;
class RecWriteMain;
class LauncherMain;
}  // namespace mister::app
namespace mister::svc {
class PrefetchMain;
class IoMain;
}  // namespace mister::svc
namespace mister::reactor {
class FrameMain;
}

namespace mister::fw {

enum class RtMode : std::uint8_t;

class ThreadAssembly {
    TASTY_SEAT_EXEMPT(boot);

public:
    ThreadAssembly(const hal::ThreadMap& threads, xthread::RtStats& stats, app::EventQueue& events,
                   RtEvidence& boot, xthread::WakeFlag& main_wake,
                   const UiMain::Wiring& ui) noexcept;

    ThreadAssembly(const hal::ThreadMap&, xthread::RtStats&, app::EventQueue&, RtEvidence&&,
                   xthread::WakeFlag&, const UiMain::Wiring&) = delete;

    ThreadAssembly(xthread::RtStats& stats, app::EventQueue& events,
                   const UiMain::Wiring& ui = {}) noexcept;
    ~ThreadAssembly();

    ThreadAssembly(const ThreadAssembly&) = delete;
    ThreadAssembly& operator=(const ThreadAssembly&) = delete;

    DiagSampler& diag() noexcept { return diag_sampler_; }
    const DiagSampler& diag() const noexcept { return diag_sampler_; }
    UiMain& ui() noexcept { return ui_main_; }

    struct SeatMains {
        svc::PrefetchMain* prefetch = nullptr;
        app::PcmMain* pcm = nullptr;
        reactor::FrameMain* frame = nullptr;
        app::InputMain* input = nullptr;
        app::InputBuild* input_build = nullptr;
        svc::IoMain* io = nullptr;

        app::CaptureMain* capture = nullptr;
        app::EncodeMain* encode = nullptr;
        app::RecWriteMain* rec_write = nullptr;

        app::LauncherMain* launcher = nullptr;
    };

    Ex<void> spawn(RtMode mode, const SeatMains& mains);

    [[nodiscard]] bool any_live() const noexcept;

    [[nodiscard]] Ex<void> spawn_rt(RtMode mode, app::RtMain& rt);

    xthread::WakeFlag& supervisor_wake() noexcept { return main_wake_; }

    [[nodiscard]] bool rt_exited() const noexcept {
        return rt_exited_.load(std::memory_order_acquire);
    }

    [[nodiscard]] Ex<void> join_rt_within(std::int64_t deadline_ns);
    [[nodiscard]] Ex<void> join_io_within(std::int64_t deadline_ns);

    [[nodiscard]] Ex<void> stop_and_join_rt(std::int64_t deadline_ns);

    [[nodiscard]] Ex<void> rt_result() const noexcept { return rt_result_; }

    void stop() noexcept;
    Ex<void> join();

    void mark_quiescing() noexcept;

    void mark_transitioning(bool on) noexcept;

    const RtEvidence& evidence() const noexcept { return ev_; }

    long diag_tid() const noexcept { return diag_tid_.load(std::memory_order_acquire); }
    long ui_tid() const noexcept { return ui_tid_.load(std::memory_order_acquire); }
    long frame_tid() const noexcept { return frame_tid_.load(std::memory_order_acquire); }
    long input_tid() const noexcept { return input_tid_.load(std::memory_order_acquire); }
    long prefetch_tid() const noexcept { return prefetch_tid_.load(std::memory_order_acquire); }
    long pcm_tid() const noexcept { return pcm_tid_.load(std::memory_order_acquire); }
    long io_tid() const noexcept { return io_tid_.load(std::memory_order_acquire); }
    long capture_tid() const noexcept { return capture_tid_.load(std::memory_order_acquire); }
    long encode_tid() const noexcept { return encode_tid_.load(std::memory_order_acquire); }
    long rec_write_tid() const noexcept { return rec_write_tid_.load(std::memory_order_acquire); }
    long launcher_tid() const noexcept { return launcher_tid_.load(std::memory_order_acquire); }

    [[nodiscard]] bool io_wedged() const noexcept { return io_poisoned_; }

    [[nodiscard]] bool has_io_seat() const noexcept { return io_ != nullptr; }
    long rt_tid() const noexcept { return rt_tid_.load(std::memory_order_acquire); }

private:
    void bind_ui_cells_(const UiMain::Wiring& ui) noexcept;

    void adopt_seat(hal::Seat s) noexcept;

    RtSetup* affinity_row(hal::Seat s) noexcept;

    template <class M>
    static void* trampoline(void* self);

    DiagMain& main_(std::type_identity<DiagMain>) noexcept;
    UiMain& main_(std::type_identity<UiMain>) noexcept;
    svc::PrefetchMain& main_(std::type_identity<svc::PrefetchMain>) noexcept;
    app::PcmMain& main_(std::type_identity<app::PcmMain>) noexcept;
    reactor::FrameMain& main_(std::type_identity<reactor::FrameMain>) noexcept;
    app::InputMain& main_(std::type_identity<app::InputMain>) noexcept;
    svc::IoMain& main_(std::type_identity<svc::IoMain>) noexcept;
    app::RtMain& main_(std::type_identity<app::RtMain>) noexcept;
    app::CaptureMain& main_(std::type_identity<app::CaptureMain>) noexcept;
    app::EncodeMain& main_(std::type_identity<app::EncodeMain>) noexcept;
    app::RecWriteMain& main_(std::type_identity<app::RecWriteMain>) noexcept;
    app::LauncherMain& main_(std::type_identity<app::LauncherMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<DiagMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<UiMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<svc::PrefetchMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::PcmMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<reactor::FrameMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::InputMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<svc::IoMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::RtMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::CaptureMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::EncodeMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::RecWriteMain>) noexcept;
    static constexpr hal::Seat seat_(std::type_identity<app::LauncherMain>) noexcept;

    std::atomic<long>* tid_cell_(hal::Seat s) noexcept;

    app::RtMain* rt_ = nullptr;
    app::InputMain* input_ = nullptr;
    svc::PrefetchMain* prefetch_ = nullptr;
    app::PcmMain* pcm_ = nullptr;
    svc::IoMain* io_ = nullptr;
    reactor::FrameMain* frame_ = nullptr;
    app::CaptureMain* capture_ = nullptr;
    app::EncodeMain* encode_ = nullptr;
    app::RecWriteMain* rec_write_ = nullptr;
    app::LauncherMain* launcher_ = nullptr;

    RtEvidence own_ev_{};
    RtEvidence& ev_;
    const hal::ThreadMap& threads_;

    pthread_t diag_thread_{};
    pthread_t ui_thread_{};
    pthread_t frame_thread_{};
    pthread_t input_thread_{};
    pthread_t prefetch_thread_{};
    pthread_t pcm_thread_{};
    pthread_t io_thread_{};
    pthread_t rt_thread_{};
    pthread_t capture_thread_{};
    pthread_t encode_thread_{};
    pthread_t rec_write_thread_{};
    pthread_t launcher_thread_{};
    bool diag_live_ = false;
    bool ui_live_ = false;
    bool frame_live_ = false;
    bool input_live_ = false;
    bool prefetch_live_ = false;
    bool pcm_live_ = false;
    bool io_live_ = false;
    bool io_poisoned_ = false;
    bool rt_live_ = false;
    bool capture_live_ = false;
    bool encode_live_ = false;
    bool rec_write_live_ = false;
    bool launcher_live_ = false;

    std::atomic<long> diag_tid_{0};
    std::atomic<long> ui_tid_{0};
    std::atomic<long> frame_tid_{0};
    std::atomic<long> input_tid_{0};
    std::atomic<long> prefetch_tid_{0};
    std::atomic<long> pcm_tid_{0};
    std::atomic<long> io_tid_{0};
    std::atomic<long> rt_tid_{0};
    std::atomic<long> capture_tid_{0};
    std::atomic<long> encode_tid_{0};
    std::atomic<long> rec_write_tid_{0};
    std::atomic<long> launcher_tid_{0};

    Ex<void> rt_result_{};
    xthread::WakeFlag own_main_wake_{};
    xthread::WakeFlag& main_wake_;

    std::atomic<bool> rt_exited_{false};
    std::atomic<bool> io_exited_{false};
    xthread::WakeFlag quiescing_;

    std::atomic<bool> transitioning_{false};

    DiagSampler diag_sampler_;
    DiagMain diag_main_;
    UiMain ui_main_;
};

}  // namespace mister::fw
