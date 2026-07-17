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

#include "synaxis/artwork.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace synaxis {

namespace {

// FNV-1a. Chosen over std::hash because this value names a file that outlives
// the process: std::hash offers no stability guarantee across runs or standard
// library versions, so using it would silently orphan the whole cache on a
// toolchain upgrade. FNV-1a is fixed by its specification and trivial to
// reimplement if the cache ever has to be read by something else.
std::uint64_t Fnv1a64(std::string_view data) {
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    std::uint64_t hash = kOffsetBasis;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= kPrime;
    }
    return hash;
}

std::string ToHex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

// Whether `marker` exists and is younger than the retry window.
//
// An unreadable or future-dated mtime counts as stale: the failure mode of
// getting this wrong is a provider being skipped when it shouldn't be, and a
// needless lookup is cheaper than artwork that never appears.
bool MissIsFresh(const std::filesystem::path& marker) {
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(marker, ec);
    if (ec) return false;

    const auto age = std::filesystem::file_time_type::clock::now() - written;
    if (age < decltype(age)::zero()) return false;

    return age < std::chrono::hours(24 * ArtworkCache::kMissRetryDays);
}

// Leaves an empty marker file; only its mtime matters.
void RecordMiss(const std::filesystem::path& marker) {
    std::error_code ec;
    std::ofstream out(marker);
    if (!out) return;
    out.close();

    // An existing marker keeps its original mtime otherwise, so a provider that
    // keeps missing would be retried forever once the first marker aged out.
    std::filesystem::last_write_time(marker, std::filesystem::file_time_type::clock::now(), ec);
}

} // namespace

std::filesystem::path ArtworkCache::DefaultCacheDir() {
    if (const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
        xdg_cache_home && *xdg_cache_home) {
        return std::filesystem::path(xdg_cache_home) / "synaxis" / "artwork";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".cache" / "synaxis" / "artwork";
    }
    return std::filesystem::path("artwork");
}

ArtworkCache::ArtworkCache(std::vector<std::unique_ptr<ArtworkProvider>> providers,
                            std::filesystem::path cache_dir)
    : providers_(std::move(providers)), cache_dir_(std::move(cache_dir)) {}

std::filesystem::path ArtworkCache::PathFor(const MediaEntry& entry,
                                             std::string_view provider) const {
    // Keyed on the path rather than the parsed title: the title is a guess that
    // changes whenever the parser improves, and a key that moves under the
    // cache would strand every existing tile. MediaEntry::path is canonical
    // (Scan guarantees it) so the same file always lands on the same key.
    return cache_dir_ /
           (ToHex(Fnv1a64(entry.path.string())) + "." + std::string(provider) + ".jpg");
}

std::filesystem::path ArtworkCache::PathForMiss(const MediaEntry& entry,
                                                 std::string_view provider) const {
    return cache_dir_ /
           (ToHex(Fnv1a64(entry.path.string())) + "." + std::string(provider) + ".miss");
}

std::optional<std::filesystem::path> ArtworkCache::Cached(const MediaEntry& entry) const {
    std::error_code ec;
    for (const auto& provider : providers_) {
        const std::filesystem::path path = PathFor(entry, provider->Name());
        if (std::filesystem::exists(path, ec)) return path;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ArtworkCache::Get(const MediaEntry& entry) {
    std::error_code ec;

    for (const auto& provider : providers_) {
        const std::filesystem::path cached = PathFor(entry, provider->Name());

        // This provider already produced a tile. Nothing better can be
        // upstream of it — the loop is in priority order — so this is the
        // answer.
        if (std::filesystem::exists(cached, ec)) return cached;

        // It recently had nothing to offer. Skip it rather than pay for the
        // same lookup on every launch, until the marker ages out.
        if (MissIsFresh(PathForMiss(entry, provider->Name()))) continue;

        std::filesystem::create_directories(cache_dir_, ec);
        if (ec) return std::nullopt;

        // Providers write to a temp path that's renamed into place only on
        // success. A half-written JPEG left by a crash or a provider that
        // failed midway would otherwise be indistinguishable from a good tile
        // forever after — exists() is the only check the fast path makes.
        const std::filesystem::path temp = cached.string() + ".tmp";
        std::filesystem::remove(temp, ec);

        const bool fetched = provider->Fetch(entry, temp) &&
                             std::filesystem::exists(temp, ec);  // may claim success, write nothing
        if (fetched) {
            std::filesystem::rename(temp, cached, ec);
            if (!ec) return cached;
        }

        std::filesystem::remove(temp, ec);
        RecordMiss(PathForMiss(entry, provider->Name()));
    }

    return std::nullopt;
}

} // namespace synaxis
