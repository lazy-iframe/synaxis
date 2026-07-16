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

#include "synaxis/filename_parser.hpp"

#include <filesystem>
#include <vector>

namespace synaxis {

// One file in the media library: its resolved on-disk location paired with
// metadata extracted from its filename. `path` is canonicalized at scan
// time so playback later (via libmpv) doesn't depend on the working
// directory the app happens to be launched from.
struct MediaEntry {
    std::filesystem::path path;
    ParsedFilename metadata;

    bool operator==(const MediaEntry&) const = default;
};

class MediaLibrary {
public:
    // The built-in video extensions (without leading dot) that Scan() filters
    // by default. Also the universe of valid values for a caller-supplied
    // extension list — callers should validate any user-supplied extensions
    // against this list before passing them to Scan().
    static const std::vector<std::string>& DefaultVideoExtensions();

    // Recursively scans `root` for regular files whose extension is in
    // `extensions` (case-insensitive, without the leading dot), parsing each
    // matching filename via FilenameParser and resolving its path to
    // canonical/absolute form. An empty `extensions` (the default) falls
    // back to DefaultVideoExtensions().
    static std::vector<MediaEntry> Scan(const std::filesystem::path& root,
                                         const std::vector<std::string>& extensions = {});

    // Serializes entries to a JSON file at `path`, so the library survives
    // across runs without re-scanning/re-parsing the whole directory tree.
    static void SaveToJson(const std::vector<MediaEntry>& entries,
                            const std::filesystem::path& path);

    // Loads entries previously written by SaveToJson.
    static std::vector<MediaEntry> LoadFromJson(const std::filesystem::path& path);
};

} // namespace synaxis
