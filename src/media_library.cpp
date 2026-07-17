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

#include "synaxis/media_library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <tuple>

#include <nlohmann/json.hpp>

namespace synaxis {

namespace {

using nlohmann::json;

// Cheap pre-filter so the (relatively expensive) FilenameParser regexes only
// run on files that are actually video — skips scanning past subtitles,
// artwork, .nfo files, etc. in large libraries. `extensions` is always
// either DefaultVideoExtensions() or a caller-validated subset of it (see
// MediaLibrary::DefaultVideoExtensions()), so it's safe to splice directly
// into the pattern without escaping.
std::regex BuildVideoExtensionRegex(const std::vector<std::string>& extensions) {
    std::string pattern = R"(\.()";
    for (size_t i = 0; i < extensions.size(); ++i) {
        if (i) pattern += '|';
        pattern += extensions[i];
    }
    pattern += R"()$)";
    return std::regex(pattern, std::regex::icase);
}

bool ContainsIgnoreCase(const std::string& haystack, const std::string& needle) {
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                          [](unsigned char x, unsigned char y) {
                              return std::tolower(x) == std::tolower(y);
                          });
    return it != haystack.end();
}

// Series are keyed case-insensitively: the same show is spelled inconsistently
// across releases ("crimson static" next to "Crimson Static"), and grouping on
// the raw title would split it into two series.
std::string FoldCase(const std::string& value) {
    std::string folded;
    folded.reserve(value.size());
    for (unsigned char c : value) folded += static_cast<char>(std::tolower(c));
    return folded;
}

json ToJson(const MediaEntry& entry) {
    const ParsedFilename& m = entry.metadata;
    return json{
        {"path", entry.path.string()},
        {"season", m.season ? json(*m.season) : json(nullptr)},
        {"episode", m.episode ? json(*m.episode) : json(nullptr)},
        {"year", m.year ? json(*m.year) : json(nullptr)},
        {"source", m.source ? json(*m.source) : json(nullptr)},
        {"title", m.title ? json(*m.title) : json(nullptr)},
        {"tags", m.tags},
    };
}

MediaEntry FromJson(const json& j) {
    MediaEntry entry;
    entry.path = j.at("path").get<std::string>();
    if (!j.at("season").is_null()) entry.metadata.season = j.at("season").get<int>();
    if (!j.at("episode").is_null()) entry.metadata.episode = j.at("episode").get<int>();
    if (!j.at("year").is_null()) entry.metadata.year = j.at("year").get<int>();
    if (!j.at("source").is_null()) entry.metadata.source = j.at("source").get<std::string>();
    if (!j.at("title").is_null()) entry.metadata.title = j.at("title").get<std::string>();
    entry.metadata.tags = j.at("tags").get<std::vector<std::string>>();
    return entry;
}

} // namespace

const std::vector<std::string>& MediaLibrary::DefaultVideoExtensions() {
    static const std::vector<std::string> kDefaultVideoExtensions = {
        "mp4", "mkv", "mov", "avi", "webm", "wmv", "flv", "m4v", "mpg", "mpeg", "ts",
    };
    return kDefaultVideoExtensions;
}

std::filesystem::path MediaLibrary::DefaultLibraryPath() {
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
        xdg_data_home && *xdg_data_home) {
        return std::filesystem::path(xdg_data_home) / "synaxis" / "library.json";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "synaxis" / "library.json";
    }
    // No HOME to anchor to: fall back to the working directory rather than
    // writing somewhere unpredictable.
    return std::filesystem::path("library.json");
}

std::vector<MediaEntry> MediaLibrary::Scan(const std::filesystem::path& root,
                                            const std::vector<std::string>& extensions,
                                            const ScanProgressCallback& on_progress) {
    const std::regex video_extension =
        BuildVideoExtensionRegex(extensions.empty() ? DefaultVideoExtensions() : extensions);

    std::vector<MediaEntry> entries;
    ScanProgress progress;

    for (const auto& dir_entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!dir_entry.is_regular_file()) continue;

        ++progress.files_seen;
        progress.current = dir_entry.path();

        if (std::regex_search(dir_entry.path().filename().string(), video_extension)) {
            MediaEntry entry;
            entry.path = std::filesystem::canonical(dir_entry.path());
            entry.metadata = FilenameParser::parse(dir_entry.path().filename().string());
            entries.push_back(std::move(entry));
            ++progress.files_indexed;
        }

        if (on_progress && !on_progress(progress)) break;
    }

    std::sort(entries.begin(), entries.end(),
              [](const MediaEntry& a, const MediaEntry& b) { return a.path < b.path; });
    return entries;
}

std::vector<MediaEntry> MediaLibrary::Find(const std::vector<MediaEntry>& entries,
                                            const MediaQuery& query) {
    std::vector<MediaEntry> candidates;
    for (const auto& entry : entries) {
        const auto& m = entry.metadata;
        if (!m.title || !ContainsIgnoreCase(*m.title, query.name)) continue;

        if (query.episode) {
            // TV lookup: season/episode must match exactly.
            if (m.season != query.episode->season || m.episode != query.episode->episode) continue;
        } else {
            // Movie lookup: exclude anything that looks like a TV episode,
            // and narrow by year if the caller gave one.
            if (m.season || m.episode) continue;
            if (query.year && m.year != *query.year) continue;
        }

        candidates.push_back(entry);
    }
    return candidates;
}

LibraryTree MediaLibrary::Group(const std::vector<MediaEntry>& entries) {
    LibraryTree tree;

    // Keyed on the folded title, but each series remembers the first spelling
    // seen so the UI shows a real title rather than a lowercased one.
    std::map<std::string, Series> series_by_key;
    std::map<std::string, std::map<int, std::vector<MediaEntry>>> episodes_by_key;

    for (const auto& entry : entries) {
        const auto& m = entry.metadata;
        const bool is_episode = m.season && m.episode && m.title;
        if (!is_episode) {
            tree.movies.push_back(entry);
            continue;
        }

        const std::string key = FoldCase(*m.title);
        series_by_key.try_emplace(key, Series{*m.title, {}});
        episodes_by_key[key][*m.season].push_back(entry);
    }

    for (auto& [key, series] : series_by_key) {
        for (auto& [season_number, episodes] : episodes_by_key[key]) {
            std::sort(episodes.begin(), episodes.end(),
                      [](const MediaEntry& a, const MediaEntry& b) {
                          return std::tie(*a.metadata.episode, a.path) <
                                 std::tie(*b.metadata.episode, b.path);
                      });
            series.seasons.push_back(Season{season_number, std::move(episodes)});
        }
        tree.series.push_back(std::move(series));
    }

    // series_by_key is already ordered by folded title, which is the order we
    // want; movies need explicit ordering since they arrive path-sorted.
    std::sort(tree.movies.begin(), tree.movies.end(), [](const MediaEntry& a, const MediaEntry& b) {
        const std::string a_title = a.metadata.title ? FoldCase(*a.metadata.title) : std::string();
        const std::string b_title = b.metadata.title ? FoldCase(*b.metadata.title) : std::string();
        const int a_year = a.metadata.year.value_or(0);
        const int b_year = b.metadata.year.value_or(0);
        return std::tie(a_title, a_year, a.path) < std::tie(b_title, b_year, b.path);
    });

    return tree;
}

void MediaLibrary::SaveToJson(const std::vector<MediaEntry>& entries,
                               const std::filesystem::path& path) {
    json arr = json::array();
    for (const auto& entry : entries) arr.push_back(ToJson(entry));

    if (path.has_parent_path() && !path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream out(path);
    out << json{{"entries", arr}}.dump(2);
}

std::vector<MediaEntry> MediaLibrary::LoadFromJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    json root;
    in >> root;

    std::vector<MediaEntry> entries;
    for (const auto& j : root.at("entries")) entries.push_back(FromJson(j));
    return entries;
}

} // namespace synaxis
