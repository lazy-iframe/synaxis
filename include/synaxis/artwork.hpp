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

#include "synaxis/media_library.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace synaxis {

// Artwork is 16:9 throughout, at kArtworkWidth.
//
// This is what makes a mixed grid possible. Tiles come from different sources —
// a real backdrop for a matched title, an extracted frame for an unmatched one —
// and a row of mismatched aspect ratios reads as broken. Landscape rather than
// portrait because an extracted frame *is* 16:9: cropping one into a 2:3 poster
// keeps barely a third of its width and destroys the shot. It also happens to
// be what <inspiration>'s own desktop rows use.
inline constexpr int kArtworkWidth = 640;

// Supplies artwork for a library entry. Implementations are tried in order by
// ArtworkCache and may decline, which is ordinary rather than exceptional: a
// title TMDB has never heard of, no network, an unreadable file.
class ArtworkProvider {
public:
    virtual ~ArtworkProvider() = default;

    // Writes a 16:9 image for `entry` to `out_path`, whose parent directory
    // already exists. Returns false to decline, leaving `out_path` untouched
    // and passing the entry to the next provider.
    //
    // May block — decoding a frame or making a network round trip both take
    // real time. Callers run this off the UI thread.
    virtual bool Fetch(const MediaEntry& entry, const std::filesystem::path& out_path) = 0;

    // For diagnostics; identifies which link in the chain produced a tile.
    virtual std::string_view Name() const = 0;
};

// Extracts a frame from the video itself via libmpv, decoding headlessly.
// The last resort in the chain, and the only provider that always works
// offline with no configuration — every playable file has a frame in it.
//
// Returns nullptr if libmpv can't be initialized at all.
std::unique_ptr<ArtworkProvider> MakeFrameProvider();

// A provider chain fronted by an on-disk cache.
//
// Not thread-safe, matching MediaLibrary::Scan's stance that the caller owns
// threading. Get() blocks for as long as its providers do, so a GUI runs one
// of these on a worker thread and hands results back to the UI thread.
class ArtworkCache {
public:
    // $XDG_CACHE_HOME/synaxis/artwork/, falling back to ~/.cache/synaxis/artwork/.
    // Cache rather than data: every tile here can be regenerated from the
    // media, so it's exactly what XDG_CACHE_HOME is for and it's safe for the
    // user (or the system) to delete. Contrast WatchStore::DefaultPath().
    static std::filesystem::path DefaultCacheDir();

    // How long a provider's miss is trusted before it's asked again. Days
    // rather than forever, because "no match" is a fact about TMDB's catalogue
    // and the network on one particular afternoon, not about the file.
    static constexpr int kMissRetryDays = 7;

    // Providers are consulted in the order given.
    explicit ArtworkCache(std::vector<std::unique_ptr<ArtworkProvider>> providers,
                          std::filesystem::path cache_dir = DefaultCacheDir());

    // The cached artwork path for `entry`, generating it on first call.
    // nullopt means every provider declined.
    //
    // Providers are consulted in order, and each caches under its own key, so a
    // better provider always gets its say. Keying the cache on the file alone
    // would mean whichever provider happened to win first owns that tile
    // forever: configure a TMDB key after a library has been browsed once and
    // every existing tile stays an extracted frame, because nothing ever asks
    // TMDB again. The provider is part of the identity of a cached image, not
    // an implementation detail of how it was made.
    //
    // A miss is remembered only briefly (see PathForMiss): long enough that an
    // unmatched library doesn't re-query TMDB on every launch, and not so long
    // that a title added to TMDB next month never shows up.
    std::optional<std::filesystem::path> Get(const MediaEntry& entry);

    // The artwork already on disk for `entry`, best provider first, without
    // fetching anything. nullopt when nothing is cached yet.
    //
    // Pure: creates nothing and consults no provider, so it's safe to call from
    // a UI thread to decide what to paint before the worker has run.
    std::optional<std::filesystem::path> Cached(const MediaEntry& entry) const;

    // Where `provider`'s artwork for `entry` lives, whether or not it exists.
    std::filesystem::path PathFor(const MediaEntry& entry, std::string_view provider) const;

    // Where `provider`'s "nothing here" marker for `entry` lives. Its mtime is
    // the record: a marker older than the retry window is ignored, which is what
    // lets a transient failure (TMDB unreachable) expire on its own rather than
    // becoming permanent.
    std::filesystem::path PathForMiss(const MediaEntry& entry, std::string_view provider) const;

private:
    std::vector<std::unique_ptr<ArtworkProvider>> providers_;
    std::filesystem::path cache_dir_;
};

} // namespace synaxis
