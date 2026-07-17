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

#pragma once

#include <filesystem>
#include <functional>
#include <memory>

namespace synaxis {

// Drives playback of a single file at a time.
//
// Control is asynchronous: Open() starts playback and returns, and progress
// arrives through a status callback. A blocking API can be built on top of
// that (see WaitUntilFinished) but the reverse isn't true, which is why this
// is the shape — a UI can't sit inside a call that only returns when the
// film ends.
//
// Both backends currently open their own window. Rendering into a
// caller-supplied surface is a separate step and, by design, changes only
// the backend rather than this interface.
class Player {
public:
    // The media backend used to play a file.
    //
    // Mpv drives libmpv, whose window carries its usual keyboard bindings and
    // OSD. Vlc drives libvlc, whose window has neither and can only be
    // closed, because libvlc leaves that surface to the embedding
    // application. Mpv is the default and the intended foundation for the
    // GUI: its render API embeds natively on Wayland, which libvlc 3.x
    // cannot. Vlc is kept as a working comparison.
    enum class Backend { Mpv, Vlc };

    enum class State {
        Idle,     // nothing loaded yet
        Loading,  // Open() accepted, first frame not yet decoded
        Playing,
        Paused,
        Ended,    // reached end of file, or was stopped
        Error,    // playback failed
    };

    // A snapshot of playback. `duration` is 0 until the backend has worked it
    // out, and stays 0 for streams that never report one.
    struct Status {
        State state = State::Idle;
        double position = 0.0;  // seconds
        double duration = 0.0;  // seconds; 0 when unknown

        bool operator==(const Status&) const = default;
    };

    // Invoked whenever the status changes.
    //
    // Called from a backend thread — libmpv's event pump or one of libvlc's
    // internal threads — never from the thread that called Open(). It must
    // not block and must not re-enter Player. A Qt front-end should do
    // nothing here but emit a queued signal, hopping the value onto the UI
    // thread.
    using StatusCallback = std::function<void(const Status&)>;

    explicit Player(Backend backend = Backend::Mpv);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Must be set before Open() to avoid missing early transitions.
    void SetStatusCallback(StatusCallback callback);

    // Starts playing `path` and returns immediately. False means the file
    // could not be handed to the backend at all; a failure discovered later
    // during decoding surfaces as State::Error instead.
    bool Open(const std::filesystem::path& path);

    void Pause();
    void Resume();
    void Stop();

    // Absolute position, in seconds, clamped by the backend.
    void Seek(double seconds);

    Status GetStatus() const;

    // Blocks until playback reaches Ended or Error. Returns false if
    // playback failed. This is what lets the CLI keep its
    // "play and wait for the window to close" behaviour on an async core;
    // a GUI uses the status callback instead and never calls this.
    bool WaitUntilFinished();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace synaxis
