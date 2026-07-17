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

    virtual bool Open(const std::filesystem::path& path) = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Stop() = 0;
    virtual void Seek(double seconds) = 0;
};

// Both return nullptr if the underlying library can't be initialized.
std::unique_ptr<PlayerBackend> MakeMpvBackend(StatusHandler on_status);
std::unique_ptr<PlayerBackend> MakeVlcBackend(StatusHandler on_status);

} // namespace synaxis::detail
