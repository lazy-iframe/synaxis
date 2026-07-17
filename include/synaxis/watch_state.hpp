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

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace synaxis {

// How far into one file playback got, and when. `path` is the canonical media
// path, matching MediaEntry::path, so a record can be joined back onto the
// library by value.
//
// `duration` is stored alongside `position` rather than looked up from the
// file because a resume row has to render a progress bar before the player has
// opened anything — re-deriving the duration would mean decoding every file on
// the shelf just to draw it.
struct WatchRecord {
    std::filesystem::path path;
    double position = 0.0;         // seconds
    double duration = 0.0;         // seconds; 0 when the backend never reported one
    std::int64_t last_played = 0;  // Unix seconds

    bool operator==(const WatchRecord&) const = default;
};

// Remembers playback positions across runs so the UI can offer a "Continue
// Watching" shelf and resume mid-file.
//
// Not thread-safe, and deliberately so: Player reports status from a backend
// thread, and a store that locked internally would invite callers to write to
// it from there. The intended shape is the one Player's docs already describe —
// hop the status onto the UI thread, then touch the store from that one thread.
//
// Persistence is explicit rather than automatic on every update: positions
// arrive several times a second during playback, and writing the file that
// often would be pointless I/O. Call Save() at the moments that matter (pause,
// stop, end of file, shutdown).
class WatchStore {
public:
    // Fraction of a file below which playback is treated as "not really
    // started" and above which it's treated as finished. Continue-watching
    // shelves exist to resume things you're partway through: an accidental
    // press two seconds in isn't progress, and a file you watched to the end
    // is done. <inspiration> behaves the same way, and the thresholds are the
    // reason the shelf stays useful rather than becoming a history log.
    static constexpr double kStartedFraction = 0.02;
    static constexpr double kFinishedFraction = 0.92;

    // Where watch state lives by default: alongside library.json under
    // $XDG_DATA_HOME/synaxis/, falling back to ~/.local/share/synaxis/.
    // It's data rather than cache — a resume point is not something the user
    // can regenerate by rescanning — so it belongs in XDG_DATA_HOME and not
    // XDG_CACHE_HOME, unlike artwork.
    static std::filesystem::path DefaultPath();

    // A missing file yields an empty store rather than throwing: no watch
    // state is the normal condition on a first run, not an error. A file that
    // exists but is corrupt still throws, since that's a real problem worth
    // surfacing.
    static WatchStore LoadFromJson(const std::filesystem::path& path);

    void SaveToJson(const std::filesystem::path& path) const;

    // Records progress for `media`, replacing any previous record. `when`
    // defaults to now; it's injectable so tests don't depend on the clock.
    void Record(const std::filesystem::path& media, double position, double duration,
                std::int64_t when = 0);

    // Drops any record for `media` — what "remove from Continue Watching"
    // calls, and what a caller should do when a file no longer exists.
    void Forget(const std::filesystem::path& media);

    std::optional<WatchRecord> Find(const std::filesystem::path& media) const;

    // Every record, unfiltered and unordered. For persistence and for callers
    // that want their own policy; the shelf wants ContinueWatching().
    const std::vector<WatchRecord>& Records() const { return records_; }

    // Records worth resuming — started but not finished per the fractions
    // above — most recently played first, which is the order the shelf shows.
    // Records whose duration is unknown (0) are included: something was
    // played, and dropping it would silently lose it.
    std::vector<WatchRecord> ContinueWatching() const;

    // Where playback should resume for `media`: its stored position, or 0 when
    // there's nothing to resume (unknown file, barely started, or finished —
    // a finished file restarts from the top rather than parking on the credits).
    double ResumePosition(const std::filesystem::path& media) const;

private:
    std::vector<WatchRecord> records_;
};

} // namespace synaxis
