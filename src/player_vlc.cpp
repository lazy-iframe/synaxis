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

#include <vlc/vlc.h>

#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace synaxis::detail {

namespace {

// Milliseconds are libvlc's unit for time and length; Player's is seconds.
constexpr double kMillisecondsPerSecond = 1000.0;

// Unlike libmpv, libvlc runs its own threads and reports through an event
// manager, so there's no pump to own here. The hard rule is the inverse: an
// event callback must never call back into libvlc, or it deadlocks. Every
// callback below therefore only publishes status.
class VlcBackend : public PlayerBackend {
public:
    VlcBackend(libvlc_instance_t* vlc, libvlc_media_player_t* player, StatusHandler on_status)
        : vlc_(vlc), player_(player), on_status_(std::move(on_status)) {
        events_ = libvlc_media_player_event_manager(player_);
        Attach(libvlc_MediaPlayerPlaying);
        Attach(libvlc_MediaPlayerPaused);
        Attach(libvlc_MediaPlayerEndReached);
        Attach(libvlc_MediaPlayerEncounteredError);
        Attach(libvlc_MediaPlayerStopped);
        Attach(libvlc_MediaPlayerTimeChanged);
        Attach(libvlc_MediaPlayerLengthChanged);
    }

    ~VlcBackend() override {
        // Detached first so a late event can't reach a half-destroyed object.
        Detach(libvlc_MediaPlayerPlaying);
        Detach(libvlc_MediaPlayerPaused);
        Detach(libvlc_MediaPlayerEndReached);
        Detach(libvlc_MediaPlayerEncounteredError);
        Detach(libvlc_MediaPlayerStopped);
        Detach(libvlc_MediaPlayerTimeChanged);
        Detach(libvlc_MediaPlayerLengthChanged);

        libvlc_media_player_stop(player_);
        libvlc_media_player_release(player_);
        libvlc_release(vlc_);
    }

    bool Open(const std::filesystem::path& path) override {
        Update([](Player::Status& s) { s = Player::Status{}; s.state = Player::State::Loading; });

        const std::string path_str = path.string();
        libvlc_media_t* media = libvlc_media_new_path(vlc_, path_str.c_str());
        if (!media) {
            std::cerr << "error: failed to load file: " << path_str << "\n";
            return false;
        }

        libvlc_media_player_set_media(player_, media);
        libvlc_media_release(media);

        if (libvlc_media_player_play(player_) < 0) {
            std::cerr << "error: failed to start playback: " << path_str << "\n";
            return false;
        }
        return true;
    }

    void Pause() override { libvlc_media_player_set_pause(player_, 1); }

    void Resume() override { libvlc_media_player_set_pause(player_, 0); }

    void Stop() override {
        // Reports back as libvlc_MediaPlayerStopped, which settles the state.
        libvlc_media_player_stop(player_);
    }

    void Seek(double seconds) override {
        libvlc_media_player_set_time(
            player_, static_cast<libvlc_time_t>(seconds * kMillisecondsPerSecond));
    }

private:
    void Attach(libvlc_event_e type) { libvlc_event_attach(events_, type, &OnEvent, this); }

    void Detach(libvlc_event_e type) { libvlc_event_detach(events_, type, &OnEvent, this); }

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

    static void OnEvent(const libvlc_event_t* event, void* opaque) {
        static_cast<VlcBackend*>(opaque)->Dispatch(*event);
    }

    void Dispatch(const libvlc_event_t& event) {
        switch (event.type) {
            case libvlc_MediaPlayerPlaying:
                Update([](Player::Status& s) { s.state = Player::State::Playing; });
                break;
            case libvlc_MediaPlayerPaused:
                Update([](Player::Status& s) { s.state = Player::State::Paused; });
                break;
            case libvlc_MediaPlayerTimeChanged: {
                const double seconds =
                    static_cast<double>(event.u.media_player_time_changed.new_time) /
                    kMillisecondsPerSecond;
                Update([seconds](Player::Status& s) { s.position = seconds; });
                break;
            }
            case libvlc_MediaPlayerLengthChanged: {
                const double seconds =
                    static_cast<double>(event.u.media_player_length_changed.new_length) /
                    kMillisecondsPerSecond;
                Update([seconds](Player::Status& s) { s.duration = seconds; });
                break;
            }
            case libvlc_MediaPlayerEncounteredError:
                std::cerr << "error: playback failed\n";
                Update([](Player::Status& s) { s.state = Player::State::Error; });
                break;
            case libvlc_MediaPlayerEndReached:
            case libvlc_MediaPlayerStopped:
                // Closing the window tears the player down without necessarily
                // reaching end-of-stream, so Stopped has to settle the state
                // too — otherwise a WaitUntilFinished() caller hangs forever.
                Update([](Player::Status& s) {
                    if (s.state != Player::State::Error) s.state = Player::State::Ended;
                });
                break;
            default:
                break;
        }
    }

    libvlc_instance_t* vlc_ = nullptr;
    libvlc_media_player_t* player_ = nullptr;
    libvlc_event_manager_t* events_ = nullptr;
    StatusHandler on_status_;

    mutable std::mutex mutex_;
    Player::Status status_;
};

} // namespace

std::unique_ptr<PlayerBackend> MakeVlcBackend(StatusHandler on_status) {
    libvlc_instance_t* vlc = libvlc_new(0, nullptr);
    if (!vlc) {
        std::cerr << "error: failed to create libvlc instance\n";
        return nullptr;
    }

    libvlc_media_player_t* player = libvlc_media_player_new(vlc);
    if (!player) {
        std::cerr << "error: failed to create libvlc player\n";
        libvlc_release(vlc);
        return nullptr;
    }

    return std::make_unique<VlcBackend>(vlc, player, std::move(on_status));
}

} // namespace synaxis::detail
