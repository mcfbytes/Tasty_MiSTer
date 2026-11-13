// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/connect_cells.h"
#include "infra/inbox.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "svc/connect_request.h"

namespace mister::app {

class ConnectChannel {
    TASTY_SEAT_MEDIATOR(Ui, Connect);

    struct Cells {
        WifiViewCell wifi{};
        WifiScanViewCell scan{};
        WifiSavedViewCell saved{};
        BtViewCell bt{};
        BtDeviceViewCell devices{};
        BtPromptViewCell prompt{};
    };

public:
    static constexpr std::size_t kRequestDepth = 8;
    using Requests = xthread::Inbox<svc::ConnectRequest, kRequestDepth>;

    class Sender {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        Sender() noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept { return requests_.has_value(); }

        [[nodiscard]] bool send(const svc::ConnectRequest& request) noexcept {
            return requests_.has_value() && requests_->push(request);
        }

    private:
        friend class ConnectChannel;

        explicit Sender(Requests& requests) noexcept : requests_(requests) {}

        infra::OptRef<Requests> requests_{};
    };

    class Viewer {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        Viewer() noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept { return cells_.has_value(); }

        [[nodiscard]] std::optional<svc::WifiView> take_wifi() noexcept {
            return cells_.has_value() ? wifi_.take_if_changed(cells_->wifi) : std::nullopt;
        }

        [[nodiscard]] std::optional<svc::WifiScanView> take_scan() noexcept {
            return cells_.has_value() ? scan_.take_if_changed(cells_->scan) : std::nullopt;
        }

        [[nodiscard]] std::optional<svc::WifiSavedView> take_saved() noexcept {
            return cells_.has_value() ? saved_.take_if_changed(cells_->saved) : std::nullopt;
        }

        [[nodiscard]] std::optional<svc::BtView> take_bt() noexcept {
            return cells_.has_value() ? bt_.take_if_changed(cells_->bt) : std::nullopt;
        }

        [[nodiscard]] std::optional<svc::BtDeviceView> take_devices() noexcept {
            return cells_.has_value() ? devices_.take_if_changed(cells_->devices) : std::nullopt;
        }

        [[nodiscard]] std::optional<svc::BtPromptView> take_prompt() noexcept {
            return cells_.has_value() ? prompt_.take_if_changed(cells_->prompt) : std::nullopt;
        }

        [[nodiscard]] bool pending() const noexcept {
            return cells_.has_value()
                && (unread_(cells_->wifi, wifi_)
                    || unread_(cells_->scan, scan_)
                    || unread_(cells_->saved, saved_)
                    || unread_(cells_->bt, bt_)
                    || unread_(cells_->devices, devices_)
                    || unread_(cells_->prompt, prompt_));
        }

    private:
        friend class ConnectChannel;

        explicit Viewer(const Cells& cells) noexcept : cells_(cells) {}

        template <class Cell, class Reader>
        [[nodiscard]] static bool unread_(const Cell& cell, const Reader& reader) noexcept {
            const std::uint32_t g = cell.generation();
            return g != 0 && g != reader.seen();
        }

        infra::OptRef<const Cells> cells_{};
        WifiViewCell::Reader wifi_{};
        WifiScanViewCell::Reader scan_{};
        WifiSavedViewCell::Reader saved_{};
        BtViewCell::Reader bt_{};
        BtDeviceViewCell::Reader devices_{};
        BtPromptViewCell::Reader prompt_{};
    };

    class Server {
        TASTY_SEAT_RESIDENT(Connect);

    public:
        Server() noexcept = default;

        [[nodiscard]] explicit operator bool() const noexcept { return requests_.has_value(); }

        [[nodiscard]] std::optional<svc::ConnectRequest> pop() noexcept {
            return requests_.has_value() ? requests_->pop() : std::nullopt;
        }

        [[nodiscard]] bool requests_empty() const noexcept {
            return !requests_.has_value() || requests_->empty();
        }

        void publish(const svc::WifiView& v) noexcept {
            if (cells_.has_value())
                cells_->wifi.publish(v);
        }

        void publish(const svc::WifiScanView& v) noexcept {
            if (cells_.has_value())
                cells_->scan.publish(v);
        }

        void publish(const svc::WifiSavedView& v) noexcept {
            if (cells_.has_value())
                cells_->saved.publish(v);
        }

        void publish(const svc::BtView& v) noexcept {
            if (cells_.has_value())
                cells_->bt.publish(v);
        }

        void publish(const svc::BtDeviceView& v) noexcept {
            if (cells_.has_value())
                cells_->devices.publish(v);
        }

        void publish(const svc::BtPromptView& v) noexcept {
            if (cells_.has_value())
                cells_->prompt.publish(v);
        }

    private:
        friend class ConnectChannel;

        Server(Requests& requests, Cells& cells) noexcept : requests_(requests), cells_(cells) {}

        infra::OptRef<Requests> requests_{};
        infra::OptRef<Cells> cells_{};
    };

    explicit ConnectChannel(xthread::WakeFlag& connect_wake) noexcept : requests_(connect_wake) {}

    ConnectChannel(const ConnectChannel&) = delete;
    ConnectChannel& operator=(const ConnectChannel&) = delete;

    [[nodiscard]] Sender sender() noexcept {
        if (std::exchange(sender_minted_, true))
            return Sender{};
        return Sender{requests_};
    }

    [[nodiscard]] Viewer viewer() noexcept {
        if (std::exchange(viewer_minted_, true))
            return Viewer{};
        return Viewer{cells_};
    }

    [[nodiscard]] Server server() noexcept {
        if (std::exchange(server_minted_, true))
            return Server{};
        return Server{requests_, cells_};
    }

private:
    Requests requests_;
    Cells cells_{};
    bool sender_minted_ = false;
    bool viewer_minted_ = false;
    bool server_minted_ = false;
};

}
