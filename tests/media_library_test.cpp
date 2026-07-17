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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace synaxis {
namespace {

namespace fs = std::filesystem;

// Scan() only reads filenames and extensions, never content, so empty files are
// a faithful fixture for everything in this file — and keep it independent of
// ffmpeg and of the gitignored sample media.
class MediaLibraryTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("synaxis-lib-test-" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path Touch(const std::string& relative) {
        const fs::path path = dir_ / relative;
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << "x";
        return fs::canonical(path);
    }

    static MediaEntry Movie(const std::string& title, int year, const fs::path& path = "/m.mkv") {
        MediaEntry entry;
        entry.path = path;
        entry.metadata.title = title;
        entry.metadata.year = year;
        return entry;
    }

    static MediaEntry Episode(const std::string& title, int season, int episode,
                              const fs::path& path = "/e.mkv") {
        MediaEntry entry;
        entry.path = path;
        entry.metadata.title = title;
        entry.metadata.season = season;
        entry.metadata.episode = episode;
        return entry;
    }

    fs::path dir_;
};

TEST_F(MediaLibraryTest, ScanFindsVideoFilesRecursively) {
    Touch("movies/Inception.2010.1080p.BluRay.mkv");
    Touch("series/show/show.s01e01.mkv");

    std::vector<MediaEntry> entries = MediaLibrary::Scan(dir_);

    EXPECT_EQ(entries.size(), 2u);
}

TEST_F(MediaLibraryTest, ScanSkipsNonVideoFiles) {
    Touch("movies/Inception.2010.mkv");
    Touch("movies/Inception.2010.srt");
    Touch("movies/poster.jpg");
    Touch("movies/movie.nfo");

    std::vector<MediaEntry> entries = MediaLibrary::Scan(dir_);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().path.extension(), ".mkv");
}

TEST_F(MediaLibraryTest, ScanMatchesExtensionsCaseInsensitively) {
    Touch("a.MKV");
    Touch("b.Mp4");

    EXPECT_EQ(MediaLibrary::Scan(dir_).size(), 2u);
}

TEST_F(MediaLibraryTest, ScanHonorsExtensionSubset) {
    Touch("a.mkv");
    Touch("b.mp4");
    Touch("c.avi");

    std::vector<MediaEntry> entries = MediaLibrary::Scan(dir_, {"mkv"});

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().path.extension(), ".mkv");
}

// Documented: an empty extension list means the defaults, not "match nothing".
TEST_F(MediaLibraryTest, ScanWithEmptyExtensionsUsesDefaults) {
    Touch("a.mkv");

    EXPECT_EQ(MediaLibrary::Scan(dir_, {}).size(), 1u);
}

// Paths are canonicalized at scan time so playback doesn't depend on the
// working directory the app happened to launch from.
TEST_F(MediaLibraryTest, ScanCanonicalizesPaths) {
    const fs::path expected = Touch("movies/film.mkv");

    std::vector<MediaEntry> entries = MediaLibrary::Scan(dir_ / "movies" / "." / "..");

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().path, expected);
    EXPECT_TRUE(entries.front().path.is_absolute());
}

TEST_F(MediaLibraryTest, ScanParsesMetadataFromFilenames) {
    Touch("Inception.2010.1080p.BluRay.mkv");

    std::vector<MediaEntry> entries = MediaLibrary::Scan(dir_);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().metadata.title, "Inception");
    EXPECT_EQ(entries.front().metadata.year, 2010);
}

TEST_F(MediaLibraryTest, ScanReportsProgress) {
    Touch("a.mkv");
    Touch("b.mkv");
    Touch("notes.txt");

    std::size_t seen = 0;
    std::size_t indexed = 0;
    MediaLibrary::Scan(dir_, {}, [&](const ScanProgress& progress) {
        seen = progress.files_seen;
        indexed = progress.files_indexed;
        return true;
    });

    EXPECT_EQ(seen, 3u) << "every regular file is visited";
    EXPECT_EQ(indexed, 2u) << "only video files are indexed";
}

// A caller that cancels knows it did: whatever was found so far comes back.
TEST_F(MediaLibraryTest, ScanStopsWhenCallbackReturnsFalse) {
    for (int i = 0; i < 10; ++i) Touch("file" + std::to_string(i) + ".mkv");

    int calls = 0;
    std::vector<MediaEntry> entries =
        MediaLibrary::Scan(dir_, {}, [&](const ScanProgress&) { return ++calls < 3; });

    EXPECT_EQ(calls, 3);
    EXPECT_LE(entries.size(), 3u);
}

TEST_F(MediaLibraryTest, FindMatchesTitleFragmentIgnoringCase) {
    std::vector<MediaEntry> library = {Movie("Dune partI", 2021), Movie("Arrival", 2016)};

    MediaQuery query;
    query.name = "dune";

    ASSERT_EQ(MediaLibrary::Find(library, query).size(), 1u);
    EXPECT_EQ(MediaLibrary::Find(library, query).front().metadata.title, "Dune partI");
}

TEST_F(MediaLibraryTest, FindNarrowsMoviesByYear) {
    std::vector<MediaEntry> library = {Movie("Dune", 1984, "/a.mkv"), Movie("Dune", 2021, "/b.mkv")};

    MediaQuery query;
    query.name = "dune";
    query.year = 2021;

    ASSERT_EQ(MediaLibrary::Find(library, query).size(), 1u);
    EXPECT_EQ(MediaLibrary::Find(library, query).front().path, "/b.mkv");
}

// A movie query must not return episodes, even when the title matches.
TEST_F(MediaLibraryTest, MovieQueryExcludesEpisodes) {
    std::vector<MediaEntry> library = {Episode("Orbit", 1, 1), Movie("Orbit", 2020)};

    MediaQuery query;
    query.name = "orbit";

    ASSERT_EQ(MediaLibrary::Find(library, query).size(), 1u);
    EXPECT_FALSE(MediaLibrary::Find(library, query).front().metadata.season.has_value());
}

TEST_F(MediaLibraryTest, EpisodeQueryMatchesSeasonAndEpisodeExactly) {
    std::vector<MediaEntry> library = {Episode("Orbit", 1, 1, "/1.mkv"),
                                        Episode("Orbit", 1, 2, "/2.mkv"),
                                        Episode("Orbit", 2, 1, "/3.mkv")};

    MediaQuery query;
    query.name = "orbit";
    query.episode = MediaQuery::EpisodeRef{1, 2};

    ASSERT_EQ(MediaLibrary::Find(library, query).size(), 1u);
    EXPECT_EQ(MediaLibrary::Find(library, query).front().path, "/2.mkv");
}

TEST_F(MediaLibraryTest, FindReturnsAllCandidatesForAmbiguousQuery) {
    std::vector<MediaEntry> library = {Movie("Dune", 1984, "/a.mkv"), Movie("Dune", 2021, "/b.mkv")};

    MediaQuery query;
    query.name = "dune";

    // Ambiguity is the caller's to resolve; Find's job is to surface every
    // candidate so a CLI prompt and a GUI list can both be built on it.
    EXPECT_EQ(MediaLibrary::Find(library, query).size(), 2u);
}

TEST_F(MediaLibraryTest, FindReturnsEmptyWhenNothingMatches) {
    std::vector<MediaEntry> library = {Movie("Arrival", 2016)};

    MediaQuery query;
    query.name = "nonexistent";

    EXPECT_TRUE(MediaLibrary::Find(library, query).empty());
}

TEST_F(MediaLibraryTest, GroupSplitsSeriesFromMovies) {
    std::vector<MediaEntry> library = {Episode("Orbit", 1, 1), Movie("Dune", 2021)};

    LibraryTree tree = MediaLibrary::Group(library);

    ASSERT_EQ(tree.series.size(), 1u);
    ASSERT_EQ(tree.movies.size(), 1u);
    EXPECT_EQ(tree.series.front().title, "Orbit");
    EXPECT_EQ(tree.movies.front().metadata.title, "Dune");
}

// The same show spelled inconsistently across releases must not split into two
// series — grouping folds case.
TEST_F(MediaLibraryTest, GroupFoldsCaseWhenKeyingSeries) {
    std::vector<MediaEntry> library = {Episode("Crimson Static", 1, 1, "/a.mkv"),
                                        Episode("crimson static", 1, 2, "/b.mkv")};

    LibraryTree tree = MediaLibrary::Group(library);

    ASSERT_EQ(tree.series.size(), 1u);
    EXPECT_EQ(tree.series.front().seasons.front().episodes.size(), 2u);
}

// ...but the first spelling seen is what's displayed, rather than a lowercased
// one.
TEST_F(MediaLibraryTest, GroupPreservesOriginalSeriesSpelling) {
    std::vector<MediaEntry> library = {Episode("Crimson Static", 1, 1, "/a.mkv"),
                                        Episode("crimson static", 1, 2, "/b.mkv")};

    EXPECT_EQ(MediaLibrary::Group(library).series.front().title, "Crimson Static");
}

TEST_F(MediaLibraryTest, GroupOrdersEpisodesByNumber) {
    std::vector<MediaEntry> library = {Episode("Orbit", 1, 3, "/c.mkv"),
                                        Episode("Orbit", 1, 1, "/a.mkv"),
                                        Episode("Orbit", 1, 2, "/b.mkv")};

    LibraryTree tree = MediaLibrary::Group(library);

    const std::vector<MediaEntry>& episodes = tree.series.front().seasons.front().episodes;
    ASSERT_EQ(episodes.size(), 3u);
    EXPECT_EQ(episodes[0].metadata.episode, 1);
    EXPECT_EQ(episodes[1].metadata.episode, 2);
    EXPECT_EQ(episodes[2].metadata.episode, 3);
}

TEST_F(MediaLibraryTest, GroupOrdersSeasonsByNumber) {
    std::vector<MediaEntry> library = {Episode("Orbit", 2, 1, "/b.mkv"),
                                        Episode("Orbit", 1, 1, "/a.mkv")};

    LibraryTree tree = MediaLibrary::Group(library);

    ASSERT_EQ(tree.series.front().seasons.size(), 2u);
    EXPECT_EQ(tree.series.front().seasons[0].number, 1);
    EXPECT_EQ(tree.series.front().seasons[1].number, 2);
}

TEST_F(MediaLibraryTest, GroupOrdersMoviesByTitleThenYear) {
    std::vector<MediaEntry> library = {Movie("Dune", 2021, "/c.mkv"), Movie("Arrival", 2016, "/a.mkv"),
                                        Movie("Dune", 1984, "/b.mkv")};

    LibraryTree tree = MediaLibrary::Group(library);

    ASSERT_EQ(tree.movies.size(), 3u);
    EXPECT_EQ(tree.movies[0].metadata.title, "Arrival");
    EXPECT_EQ(tree.movies[1].metadata.year, 1984);
    EXPECT_EQ(tree.movies[2].metadata.year, 2021);
}

// An entry whose title couldn't be parsed at all is still worth showing rather
// than vanishing, so it lands in movies.
TEST_F(MediaLibraryTest, GroupTreatsUntitledEntriesAsMovies) {
    MediaEntry untitled;
    untitled.path = "/mystery.mkv";

    LibraryTree tree = MediaLibrary::Group({untitled});

    EXPECT_TRUE(tree.series.empty());
    EXPECT_EQ(tree.movies.size(), 1u);
}

// A season marker without an episode marker isn't an episode, so it can't
// become one.
TEST_F(MediaLibraryTest, GroupTreatsSeasonWithoutEpisodeAsMovie) {
    MediaEntry entry;
    entry.path = "/show.mkv";
    entry.metadata.title = "Show";
    entry.metadata.season = 1;

    LibraryTree tree = MediaLibrary::Group({entry});

    EXPECT_TRUE(tree.series.empty());
    EXPECT_EQ(tree.movies.size(), 1u);
}

TEST_F(MediaLibraryTest, JsonRoundTripPreservesEntries) {
    Touch("Inception.2010.1080p.BluRay.PROPER.mkv");
    Touch("show/show.s01e01.WEB-DL.mkv");
    std::vector<MediaEntry> original = MediaLibrary::Scan(dir_);
    ASSERT_EQ(original.size(), 2u);

    const fs::path path = dir_ / "library.json";
    MediaLibrary::SaveToJson(original, path);

    EXPECT_EQ(MediaLibrary::LoadFromJson(path), original);
}

TEST_F(MediaLibraryTest, SaveCreatesMissingParentDirectories) {
    const fs::path path = dir_ / "nested" / "deeper" / "library.json";
    MediaLibrary::SaveToJson({}, path);

    EXPECT_TRUE(fs::exists(path));
}

TEST_F(MediaLibraryTest, JsonRoundTripPreservesEmptyLibrary) {
    const fs::path path = dir_ / "library.json";
    MediaLibrary::SaveToJson({}, path);

    EXPECT_TRUE(MediaLibrary::LoadFromJson(path).empty());
}

TEST_F(MediaLibraryTest, DefaultLibraryPathIsUnderSynaxisDataDir) {
    const fs::path path = MediaLibrary::DefaultLibraryPath();

    EXPECT_EQ(path.filename(), "library.json");
    EXPECT_EQ(path.parent_path().filename(), "synaxis");
}

// The CLI validates user-supplied extensions against this list, so it has to be
// non-empty and free of leading dots.
TEST_F(MediaLibraryTest, DefaultVideoExtensionsAreBareAndNonEmpty) {
    const std::vector<std::string>& extensions = MediaLibrary::DefaultVideoExtensions();

    ASSERT_FALSE(extensions.empty());
    EXPECT_THAT(extensions, ::testing::Contains("mkv"));
    for (const std::string& extension : extensions) {
        EXPECT_THAT(extension, ::testing::Not(::testing::StartsWith(".")));
    }
}

} // namespace
} // namespace synaxis
