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

#include "tmdb_provider.hpp"

#include <QString>
#include <QStringList>

#include <filesystem>

namespace synaxis::gui {

// Everything the settings page reads and writes, together in one struct so a
// save from one section (the API key, say) can't silently drop another (the
// scanned directories) — SaveAppConfig always writes the whole file, so every
// setter round-trips the full config through here rather than composing
// partial JSON of its own.
//
// Lives at the same path tmdb_provider.hpp already reads the API key from
// (see DefaultConfigPath there), so both old and new readers agree on where
// the file is.
struct AppConfig {
    QString tmdb_api_key;
    QStringList library_directories;  // absolute paths; RescanLibrary() walks each one

    // Extensions (lowercase, no leading dot) a scan is limited to — the GUI
    // counterpart of the CLI's -x flag. Empty means "all supported types",
    // exactly as an omitted -x does; entries are validated against
    // MediaLibrary::DefaultVideoExtensions() before they get here.
    QStringList scan_extensions;
};

// A missing or malformed file yields a default-constructed (empty) config
// rather than throwing: an unconfigured app is the normal first-run state,
// not an error.
AppConfig LoadAppConfig(const std::filesystem::path& path = DefaultConfigPath());

void SaveAppConfig(const AppConfig& config,
                    const std::filesystem::path& path = DefaultConfigPath());

} // namespace synaxis::gui
