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

#include "synaxis/artwork.hpp"

#include <QString>

#include <filesystem>
#include <memory>

namespace synaxis::gui {

// Where the API key lives: $XDG_CONFIG_HOME/synaxis/config.json, falling back
// to ~/.config/synaxis/config.json. Config rather than data or cache — it's
// user-supplied and nothing regenerates it.
//
//   { "tmdb_api_key": "..." }
std::filesystem::path DefaultConfigPath();

// Reads the key from `config_path`. Empty when the file is absent, unreadable,
// malformed, or simply has no key in it — none of which is an error worth
// reporting, because running without TMDB is a supported configuration rather
// than a broken one.
QString ReadTmdbApiKey(const std::filesystem::path& config_path = DefaultConfigPath());

// Fetches 16:9 artwork from TMDB: backdrops for films, per-episode stills for
// episodes (a season whose tiles are all the same series backdrop is a worse
// shelf than one with real stills).
//
// Returns nullptr when no API key is configured. That's the ordinary offline
// case, not a failure: the chain simply doesn't include this provider and falls
// through to frame extraction, which is why Synaxis still works with no network
// and no account.
//
// Lives in the GUI rather than in synaxis_core because it's built on
// QNetworkAccessManager, and core is deliberately Qt-free. The CLI never
// displays artwork, so nothing is lost by it being unable to fetch any.
//
// Must be constructed on the thread that will call Fetch(): it owns a
// QNetworkAccessManager, which is not shareable across threads.
std::unique_ptr<ArtworkProvider> MakeTmdbProvider(const QString& api_key);

} // namespace synaxis::gui
