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
    //
    // MpvEmbedded is Mpv with no window of its own: it decodes into whatever
    // surface the caller renders it to (see InitializeRenderer). It also has no
    // bindings and no OSD, because there's no window for them to belong to —
    // an embedding application draws its own controls.
    enum class Backend { Mpv, Vlc, MpvEmbedded };

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
    //
    // `start_position` seeks within the file as part of loading it, rather
    // than leaving the caller to Seek() once playback begins: Open() queues
    // the load and returns before the backend has anything loaded to seek
    // within, so a Seek() issued right after Open() races the load and is
    // silently dropped.
    bool Open(const std::filesystem::path& path, double start_position = 0.0);

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

    // --- Embedded rendering (Backend::MpvEmbedded only) ---
    //
    // These exist so a toolkit can draw video into its own scene without this
    // header knowing anything about that toolkit. mpv asks for GL entry points
    // through a callback rather than linking them, so the only thing crossing
    // this boundary is a function pointer — no Qt, no GL headers, no window
    // system. The other backends ignore all of it.

    // Supplies OpenGL entry points by name. Called by mpv, on the render
    // thread, while the caller's GL context is current.
    using GetProcAddress = void* (*)(void* ctx, const char* name);

    // Creates the render context. Must be called on the thread that owns the
    // GL context, with that context current, before the first RenderTo().
    // False when the backend can't render into a surface (Mpv, Vlc) or the
    // context couldn't be created.
    bool InitializeRenderer(GetProcAddress get_proc_address, void* ctx);

    // Draws the current frame into the bound framebuffer `fbo`, sized
    // `width` x `height`. Render thread only, GL context current.
    void RenderTo(int fbo, int width, int height);

    // Invoked when a new frame is ready to draw.
    //
    // Called from mpv's own thread and subject to the same rules as
    // StatusCallback: don't block, don't re-enter Player. Schedule a redraw and
    // return — the actual RenderTo() belongs on the render thread.
    void SetRenderUpdateCallback(std::function<void()> callback);

    // Destroys the render context. Same thread rules as InitializeRenderer, and
    // it must happen before the GL context goes away — the destructor can't do
    // it, because by then the render thread may be gone and this object may be
    // being destroyed on another one entirely.
    void ShutdownRenderer();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace synaxis
