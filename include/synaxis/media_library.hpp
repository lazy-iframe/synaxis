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

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace synaxis {

// One file in the media library: its resolved on-disk location paired with
// metadata extracted from its filename. `path` is canonicalized at scan
// time so playback later doesn't depend on the working directory the app
// happens to be launched from.
struct MediaEntry {
    std::filesystem::path path;
    ParsedFilename metadata;

    bool operator==(const MediaEntry&) const = default;
};

// Criteria for locating entries in a library. `name` matches any part of a
// parsed title, ignoring case, because parsed titles carry whatever noise
// survived the filename and demanding an exact reproduction makes files
// unreachable in practice.
//
// A query is either for an episode or for a movie; the two are mutually
// exclusive. Bundling season and episode into one optional makes that
// structural: a season without an episode can't be expressed, so it can't
// silently fall through to a movie lookup.
struct MediaQuery {
    struct EpisodeRef {
        int season;
        int episode;

        bool operator==(const EpisodeRef&) const = default;
    };

    std::string name;
    std::optional<EpisodeRef> episode;  // set => TV lookup
    std::optional<int> year;            // movie lookup only; narrows by year
};

// A season's episodes, ordered by episode number.
struct Season {
    int number;
    std::vector<MediaEntry> episodes;
};

// A series' seasons, ordered by season number.
struct Series {
    std::string title;
    std::vector<Season> seasons;
};

// The library arranged for browsing rather than for lookup: a flat entry
// list is what Scan() produces and what Find() searches, but a UI needs to
// walk series -> season -> episode. Holds copies rather than pointers into
// the source vector so the tree stays valid and independently ownable once
// handed to a view.
struct LibraryTree {
    std::vector<Series> series;      // ordered by title
    std::vector<MediaEntry> movies;  // ordered by title, then year
};

// Reported as Scan() walks the tree, so a caller running it off-thread can
// show progress instead of freezing.
struct ScanProgress {
    std::size_t files_seen = 0;     // regular files visited
    std::size_t files_indexed = 0;  // matched an extension and were parsed
    std::filesystem::path current;  // file just visited
};

// Return false to cancel the scan. Invoked on whichever thread is running
// Scan(); Scan() itself never spawns one.
using ScanProgressCallback = std::function<bool(const ScanProgress&)>;

class MediaLibrary {
public:
    // The built-in video extensions (without leading dot) that Scan() filters
    // by default. Also the universe of valid values for a caller-supplied
    // extension list — callers should validate any user-supplied extensions
    // against this list before passing them to Scan().
    static const std::vector<std::string>& DefaultVideoExtensions();

    // Where the serialized library lives by default:
    // $XDG_DATA_HOME/synaxis/library.json, falling back to
    // ~/.local/share/synaxis/library.json per the XDG Base Directory spec.
    // A fixed location rather than a path relative to the working directory,
    // so the CLI and the GUI see the same library regardless of how either
    // was launched.
    static std::filesystem::path DefaultLibraryPath();

    // Recursively scans `root` for regular files whose extension is in
    // `extensions` (case-insensitive, without the leading dot), parsing each
    // matching filename via FilenameParser and resolving its path to
    // canonical/absolute form. An empty `extensions` (the default) falls
    // back to DefaultVideoExtensions().
    //
    // Synchronous by design: the caller owns threading. If `on_progress`
    // returns false the walk stops early and whatever was found so far is
    // returned — a caller that cancels knows it did.
    static std::vector<MediaEntry> Scan(const std::filesystem::path& root,
                                         const std::vector<std::string>& extensions = {},
                                         const ScanProgressCallback& on_progress = {});

    // Every entry matching `query`, preserving the order of `entries`.
    // Resolving ambiguity is deliberately left to the caller: the CLI
    // prompts on stdin, a GUI shows a list. Returning all candidates is what
    // lets both do that from the same call.
    static std::vector<MediaEntry> Find(const std::vector<MediaEntry>& entries,
                                         const MediaQuery& query);

    // Arranges a flat entry list into the browsable tree. Entries carrying a
    // season and episode become series; everything else is treated as a
    // movie, including entries whose title couldn't be parsed at all — those
    // are still worth showing by filename rather than vanishing.
    static LibraryTree Group(const std::vector<MediaEntry>& entries);

    // Serializes entries to a JSON file at `path`, creating parent
    // directories as needed, so the library survives across runs without
    // re-scanning/re-parsing the whole directory tree.
    static void SaveToJson(const std::vector<MediaEntry>& entries,
                            const std::filesystem::path& path);

    // Loads entries previously written by SaveToJson.
    static std::vector<MediaEntry> LoadFromJson(const std::filesystem::path& path);
};

} // namespace synaxis
