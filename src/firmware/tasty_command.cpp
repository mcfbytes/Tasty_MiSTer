// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_command.h"

#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "app/rec_options.h"
#include "hal/board_profile.h"
#include "hal/thread_map.h"
#include "process_standup.h"
#include "replay_mode.h"
#include "svc/vfs.h"
#include "tasty_client.h"
#include "tasty_movie.h"
#include "tasty_record.h"
#include "tasty_session.h"
#include "tasty_writeback.h"

namespace mister::fw {
namespace {

tasty::Exit print_err(const Error& e, const char* what) {
    std::fprintf(
        stderr,
        "tasty: %s failed: Errc=%u site=%u detail=%u\n",
        what,
        static_cast<unsigned>(e.code),
        static_cast<unsigned>(e.site),
        static_cast<unsigned>(e.detail)
    );
    return tasty::Exit::Code::Refused;
}

[[nodiscard]] std::optional<TastyRefusal> refuse_save_outside_play(const TastyArgs& a) noexcept {
    if (a.save)
        return TastyRefusal::SaveNotPlay;
    return std::nullopt;
}

tasty::Exit info(const TastyArgs& a) {
    auto vfs = svc::Vfs::create_at("/");
    if (!vfs)
        return print_err(vfs.error(), "Vfs");
    return tasty_info_movie(*vfs, a.movie.view());
}

tasty::Exit check(const TastyArgs& a) {
    auto vfs = svc::Vfs::create_at("/");
    if (!vfs)
        return print_err(vfs.error(), "Vfs");
    return tasty_check_movie(*vfs, TastyFiles{.movie = a.movie.view(), .rom = a.rom.view()});
}

tasty::Exit rec_start_client(const TastyArgs& a, const TastyRuntime& rt) {
    if (!a.record) {
        std::fprintf(stderr, "tasty: rec start needs --record\n");
        return tasty::Exit::Code::Refused;
    }
    auto rec = tasty_prepare_record(*a.record, {});
    if (!rec)
        return tasty::Exit::Code::Refused;
    char buf[1400];
    const char* mode = a.hashes_only ? "hash" : "avi";
    const auto opt = app::format_rec_options(a.rec);
    if (opt.empty())
        return tasty::Exit::Code::Refused;
    const auto line = std::format_to_n(
        buf,
        sizeof buf,
        "rec_start {} {} {}",
        rec->view(),
        mode,
        opt.view()
    );
    if (std::cmp_greater_equal(line.size, sizeof buf))
        return tasty::Exit::Code::Refused;
    if (auto r = TastyClient{rt.paths, rt.owner}.send(std::string_view(buf, line.out)); !r)
        return print_err(r.error(), "cmd");
    if (const auto blob = hal::read_compatible(hal::kCompatiblePath)) {
        if (const auto board = hal::select_board(*blob))
            tasty_warn_record_writeback(
                rec->view(),
                hal::seat_of((*board)->threads, SeatTag::RT).cpu
            );
    }
    return tasty::Exit::Code::Ok;
}

tasty::Exit own(
    const TastyArgs& a,
    std::optional<ArgvArea> area,
    const TastySession::Run& run,
    const TastyRuntime& rt
) {
    ReplayMode mode{a, run, area, rt};
    ProcessStandup standup{mode};
    return tasty::Exit::of_status(standup.run());
}

class HelpCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(const TastyArgs&, std::optional<ArgvArea>, const TastyRuntime&) override {
        tasty_print_usage(stdout);
        return tasty::Exit::Code::Ok;
    }
};

class InfoCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(const TastyArgs& a, std::optional<ArgvArea>, const TastyRuntime&) override {
        return info(a);
    }
};

class CheckCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {
        if (a.rom.empty())
            return TastyRefusal::CheckNoRom;
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(const TastyArgs& a, std::optional<ArgvArea>, const TastyRuntime&) override {
        return check(a);
    }
};

class StatusCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(const TastyArgs&, std::optional<ArgvArea>, const TastyRuntime& rt) override {
        const TastyClient client{rt.paths, rt.owner};
        if (!client.owner_live()) {
            std::printf("idle\n");
            return tasty::Exit::Code::Ok;
        }
        const auto status = client.status();
        if (!status) {
            std::printf("idle\n");
            return tasty::Exit::Code::Ok;
        }
        std::fwrite(status->data(), 1, status->size(), stdout);
        return tasty::Exit::Code::Ok;
    }
};

class StopCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(const TastyArgs&, std::optional<ArgvArea>, const TastyRuntime& rt) override {
        const TastyClient client{rt.paths, rt.owner};
        if (!client.owner_live()) {
            std::printf("idle\n");
            return tasty::Exit::Code::Ok;
        }
        (void)client.signal_owner();
        return tasty::Exit::Code::Ok;
    }
};

class RecStopCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(const TastyArgs&, std::optional<ArgvArea>, const TastyRuntime& rt) override {
        const TastyClient client{rt.paths, rt.owner};
        if (!client.owner_live()) {
            std::printf("idle\n");
            return tasty::Exit::Code::Ok;
        }
        (void)client.send("rec_stop");
        return tasty::Exit::Code::Ok;
    }
};

class PlayCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs&) const noexcept override {
        return std::nullopt;
    }

    tasty::Exit run(
        const TastyArgs& a,
        std::optional<ArgvArea> area,
        const TastyRuntime& rt
    ) override {
        return own(a, area, run_, rt);
    }

private:
    const TastySession::PlayRun run_{};
};

class RecStartCommand final : public TastyCommand {
public:
    [[nodiscard]] std::optional<TastyRefusal> refuse(const TastyArgs& a) const noexcept override {

        if (a.rec.from_frame >= app::MovieFrame{} || a.rec.to_frame >= app::MovieFrame{}) {
            return TastyRefusal::SliceOnRecStart;
        }
        return refuse_save_outside_play(a);
    }

    tasty::Exit run(
        const TastyArgs& a,
        std::optional<ArgvArea> area,
        const TastyRuntime& rt
    ) override {
        if (TastyClient{rt.paths, rt.owner}.joins_owner())
            return rec_start_client(a, rt);
        return own(a, area, run_, rt);
    }

private:
    const TastySession::RecordOnlyRun run_{};
};

}

std::unique_ptr<TastyCommand> make_tasty_command(TastyVerb v) {
    switch (v) {
        case TastyVerb::Help:
            return std::make_unique<HelpCommand>();
        case TastyVerb::Play:
            return std::make_unique<PlayCommand>();
        case TastyVerb::Info:
            return std::make_unique<InfoCommand>();
        case TastyVerb::Check:
            return std::make_unique<CheckCommand>();
        case TastyVerb::Status:
            return std::make_unique<StatusCommand>();
        case TastyVerb::Stop:
            return std::make_unique<StopCommand>();
        case TastyVerb::RecStart:
            return std::make_unique<RecStartCommand>();
        case TastyVerb::RecStop:
            return std::make_unique<RecStopCommand>();
    }
    std::unreachable();
}

}
