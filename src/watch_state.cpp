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

#include "synaxis/watch_state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

namespace synaxis {

namespace {

using nlohmann::json;

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// True when `record` sits between the started/finished thresholds. A record
// with no known duration can't be measured against a fraction, so it counts as
// in-progress: something was played, and the alternative is losing it.
bool IsResumable(const WatchRecord& record) {
    if (record.duration <= 0.0) return true;
    const double fraction = record.position / record.duration;
    return fraction >= WatchStore::kStartedFraction && fraction < WatchStore::kFinishedFraction;
}

} // namespace

std::filesystem::path WatchStore::DefaultPath() {
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
        xdg_data_home && *xdg_data_home) {
        return std::filesystem::path(xdg_data_home) / "synaxis" / "watch.json";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "synaxis" / "watch.json";
    }
    return std::filesystem::path("watch.json");
}

WatchStore WatchStore::LoadFromJson(const std::filesystem::path& path) {
    WatchStore store;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return store;

    std::ifstream in(path);
    json root;
    in >> root;

    for (const auto& j : root.at("records")) {
        WatchRecord record;
        record.path = j.at("path").get<std::string>();
        record.position = j.at("position").get<double>();
        record.duration = j.at("duration").get<double>();
        record.last_played = j.at("last_played").get<std::int64_t>();
        store.records_.push_back(std::move(record));
    }
    return store;
}

void WatchStore::SaveToJson(const std::filesystem::path& path) const {
    json arr = json::array();
    for (const auto& record : records_) {
        arr.push_back(json{
            {"path", record.path.string()},
            {"position", record.position},
            {"duration", record.duration},
            {"last_played", record.last_played},
        });
    }

    if (path.has_parent_path() && !path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    out << json{{"records", arr}}.dump(2);
}

void WatchStore::Record(const std::filesystem::path& media, double position, double duration,
                        std::int64_t when) {
    if (when == 0) when = NowSeconds();

    auto it = std::find_if(records_.begin(), records_.end(),
                           [&media](const WatchRecord& r) { return r.path == media; });
    if (it == records_.end()) {
        records_.push_back(WatchRecord{media, position, duration, when});
        return;
    }

    it->position = position;
    it->last_played = when;
    // A backend that hasn't worked the duration out yet reports 0. Letting that
    // overwrite a good value would strand the record in the "unknown duration"
    // branch of IsResumable and lose its progress bar, so only widen.
    if (duration > 0.0) it->duration = duration;
}

void WatchStore::Forget(const std::filesystem::path& media) {
    std::erase_if(records_, [&media](const WatchRecord& r) { return r.path == media; });
}

std::optional<WatchRecord> WatchStore::Find(const std::filesystem::path& media) const {
    auto it = std::find_if(records_.begin(), records_.end(),
                           [&media](const WatchRecord& r) { return r.path == media; });
    if (it == records_.end()) return std::nullopt;
    return *it;
}

std::vector<WatchRecord> WatchStore::ContinueWatching() const {
    std::vector<WatchRecord> resumable;
    std::copy_if(records_.begin(), records_.end(), std::back_inserter(resumable), IsResumable);

    std::sort(resumable.begin(), resumable.end(), [](const WatchRecord& a, const WatchRecord& b) {
        // Ties broken on path so the shelf has a stable order rather than
        // reshuffling between launches; two files can share a timestamp
        // because last_played only has second resolution.
        if (a.last_played != b.last_played) return a.last_played > b.last_played;
        return a.path < b.path;
    });
    return resumable;
}

double WatchStore::ResumePosition(const std::filesystem::path& media) const {
    std::optional<WatchRecord> record = Find(media);
    if (!record || !IsResumable(*record)) return 0.0;
    return record->position;
}

} // namespace synaxis
