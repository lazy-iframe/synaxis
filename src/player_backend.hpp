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

#include "synaxis/player.hpp"

#include <filesystem>
#include <functional>
#include <memory>

// Each backend lives in its own translation unit so that libmpv's and
// libvlc's headers never share one — both define symbols in the global
// namespace, and neither belongs in Synaxis' public headers.
namespace synaxis::detail {

// Called by a backend whenever playback state or position moves. Runs on
// whatever thread the backend reports from, so implementations must not
// assume it's the caller's.
using StatusHandler = std::function<void(const Player::Status&)>;

// The per-backend surface. Player owns one of these and adds everything that
// isn't backend-specific (status caching, WaitUntilFinished), so a new
// backend only has to move pixels and report.
class PlayerBackend {
public:
    virtual ~PlayerBackend() = default;

    virtual bool Open(const std::filesystem::path& path, double start_position) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Stop() = 0;
    virtual void Seek(double seconds) = 0;

    // Embedded rendering. Defaulted to "can't", so a backend that brings its
    // own window — which is all of them but embedded mpv — doesn't have to say
    // so. See Player's declarations for the threading rules.
    virtual bool InitializeRenderer(Player::GetProcAddress, void*) { return false; }
    virtual void RenderTo(int /*fbo*/, int /*width*/, int /*height*/) {}
    virtual void SetRenderUpdateCallback(std::function<void()>) {}
    virtual void ShutdownRenderer() {}
};

// Both return nullptr if the underlying library can't be initialized.
//
// `embedded` gives mpv no window of its own (vo=libmpv), leaving it to render
// through the render API instead. It also drops the default key bindings and
// OSD, which belong to a window that no longer exists.
std::unique_ptr<PlayerBackend> MakeMpvBackend(StatusHandler on_status, bool embedded = false);
std::unique_ptr<PlayerBackend> MakeVlcBackend(StatusHandler on_status);

} // namespace synaxis::detail
