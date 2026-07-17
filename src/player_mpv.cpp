// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "player_backend.hpp"

#include <mpv/client.h>

#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace synaxis::detail {

namespace {

// Property names observed for status. The reply userdata ids let the event
// pump tell them apart without string comparisons.
enum PropertyId : std::uint64_t {
    kTimePos = 1,
    kDuration = 2,
    kPause = 3,
};

class MpvBackend : public PlayerBackend {
public:
    MpvBackend(mpv_handle* mpv, StatusHandler on_status)
        : mpv_(mpv), on_status_(std::move(on_status)) {
        mpv_observe_property(mpv_, kTimePos, "time-pos", MPV_FORMAT_DOUBLE);
        mpv_observe_property(mpv_, kDuration, "duration", MPV_FORMAT_DOUBLE);
        mpv_observe_property(mpv_, kPause, "pause", MPV_FORMAT_FLAG);
        pump_ = std::thread([this] { PumpEvents(); });
    }

    ~MpvBackend() override {
        // "quit" shuts the core down, which is what makes the pump's blocking
        // mpv_wait_event() return MPV_EVENT_SHUTDOWN and let the thread exit.
        // The handle is thread-safe for commands, so issuing this from here
        // while the pump is parked inside mpv_wait_event() is fine — but
        // mpv_terminate_destroy() is not, hence the join first.
        if (mpv_) {
            const char* cmd[] = {"quit", nullptr};
            mpv_command(mpv_, cmd);
        }
        if (pump_.joinable()) pump_.join();
        if (mpv_) mpv_terminate_destroy(mpv_);
    }

    bool Open(const std::filesystem::path& path) override {
        Update([](Player::Status& s) { s = Player::Status{}; s.state = Player::State::Loading; });

        const std::string path_str = path.string();
        const char* cmd[] = {"loadfile", path_str.c_str(), nullptr};
        if (mpv_command(mpv_, cmd) < 0) {
            std::cerr << "error: failed to load file: " << path_str << "\n";
            return false;
        }
        return true;
    }

    void Pause() override { SetPaused(true); }

    void Resume() override { SetPaused(false); }

    void Stop() override {
        const char* cmd[] = {"stop", nullptr};
        mpv_command(mpv_, cmd);  // arrives back as END_FILE/REASON_STOP
    }

    void Seek(double seconds) override {
        const std::string target = std::to_string(seconds);
        const char* cmd[] = {"seek", target.c_str(), "absolute", nullptr};
        mpv_command(mpv_, cmd);
    }

private:
    void SetPaused(bool paused) {
        int flag = paused ? 1 : 0;
        mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
    }

    // Applies `mutate` to the cached status and reports the result. The
    // handler is invoked with the lock released: it runs arbitrary caller
    // code and must never execute while we hold our own mutex.
    template <typename Mutate>
    void Update(Mutate&& mutate) {
        Player::Status snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            mutate(status_);
            snapshot = status_;
        }
        if (on_status_) on_status_(snapshot);
    }

    void HandlePropertyChange(const mpv_event_property& prop, std::uint64_t id) {
        // A property with no value yet (between files, say) reports
        // MPV_FORMAT_NONE with a null payload rather than being omitted.
        if (prop.format == MPV_FORMAT_NONE || !prop.data) return;

        switch (id) {
            case kTimePos:
                if (prop.format == MPV_FORMAT_DOUBLE) {
                    const double value = *static_cast<double*>(prop.data);
                    Update([value](Player::Status& s) { s.position = value; });
                }
                break;
            case kDuration:
                if (prop.format == MPV_FORMAT_DOUBLE) {
                    const double value = *static_cast<double*>(prop.data);
                    Update([value](Player::Status& s) { s.duration = value; });
                }
                break;
            case kPause:
                if (prop.format == MPV_FORMAT_FLAG) {
                    const bool paused = *static_cast<int*>(prop.data) != 0;
                    Update([paused](Player::Status& s) {
                        // Only meaningful once something is loaded; ignore the
                        // initial report that arrives before any file.
                        if (s.state == Player::State::Playing ||
                            s.state == Player::State::Paused) {
                            s.state = paused ? Player::State::Paused : Player::State::Playing;
                        }
                    });
                }
                break;
            default:
                break;
        }
    }

    void HandleEndFile(const mpv_event_end_file& end_file) {
        if (end_file.reason == MPV_END_FILE_REASON_ERROR) {
            std::cerr << "error: playback failed: " << mpv_error_string(end_file.error) << "\n";
            Update([](Player::Status& s) { s.state = Player::State::Error; });
            return;
        }
        Update([](Player::Status& s) { s.state = Player::State::Ended; });
    }

    void PumpEvents() {
        for (;;) {
            mpv_event* event = mpv_wait_event(mpv_, -1);
            switch (event->event_id) {
                case MPV_EVENT_PROPERTY_CHANGE:
                    HandlePropertyChange(*static_cast<mpv_event_property*>(event->data),
                                          event->reply_userdata);
                    break;
                case MPV_EVENT_FILE_LOADED:
                    Update([](Player::Status& s) { s.state = Player::State::Playing; });
                    break;
                case MPV_EVENT_END_FILE:
                    HandleEndFile(*static_cast<mpv_event_end_file*>(event->data));
                    break;
                case MPV_EVENT_SHUTDOWN:
                    // The window was closed, or we asked the core to quit.
                    // Either way no further events arrive, so settle in a
                    // terminal state rather than leaving a waiter stuck.
                    Update([](Player::Status& s) {
                        if (s.state != Player::State::Error) s.state = Player::State::Ended;
                    });
                    return;
                default:
                    break;
            }
        }
    }

    mpv_handle* mpv_ = nullptr;
    StatusHandler on_status_;

    mutable std::mutex mutex_;
    Player::Status status_;

    std::thread pump_;
};

} // namespace

std::unique_ptr<PlayerBackend> MakeMpvBackend(StatusHandler on_status) {
    mpv_handle* mpv = mpv_create();
    if (!mpv) {
        std::cerr << "error: failed to create mpv instance\n";
        return nullptr;
    }

    // The standalone window keeps mpv's own bindings and OSD, so the CLI
    // stays interactive. A future embedded (render API) path would turn these
    // off and supply its own controls.
    mpv_set_option_string(mpv, "input-default-bindings", "yes");
    mpv_set_option_string(mpv, "input-vo-keyboard", "yes");

    if (mpv_initialize(mpv) < 0) {
        std::cerr << "error: failed to initialize mpv\n";
        mpv_terminate_destroy(mpv);
        return nullptr;
    }

    return std::make_unique<MpvBackend>(mpv, std::move(on_status));
}

} // namespace synaxis::detail
