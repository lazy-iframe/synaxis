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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace synaxis {
namespace {

namespace fs = std::filesystem;

// A temp directory per test, so a failing test can't poison the next one and
// nothing touches the user's real $XDG_DATA_HOME.
class WatchStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("synaxis-watch-test-" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
    const fs::path film_ = "/media/film.mkv";
    const fs::path other_ = "/media/other.mkv";
};

TEST_F(WatchStateTest, RecordsAndFindsPosition) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, /*when=*/100);

    std::optional<WatchRecord> found = store.Find(film_);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->position, 300.0);
    EXPECT_EQ(found->duration, 1000.0);
    EXPECT_EQ(found->last_played, 100);
}

TEST_F(WatchStateTest, RecordReplacesRatherThanAppends) {
    WatchStore store;
    store.Record(film_, 100.0, 1000.0, 1);
    store.Record(film_, 200.0, 1000.0, 2);

    EXPECT_EQ(store.Records().size(), 1u);
    EXPECT_EQ(store.Find(film_)->position, 200.0);
}

TEST_F(WatchStateTest, FindReturnsNulloptForUnknownFile) {
    WatchStore store;
    EXPECT_FALSE(store.Find(film_).has_value());
}

// A backend that hasn't worked the duration out yet reports 0. Letting that
// land would strand the record in the unknown-duration branch and lose its
// progress bar.
TEST_F(WatchStateTest, ZeroDurationDoesNotOverwriteKnownDuration) {
    WatchStore store;
    store.Record(film_, 100.0, 1000.0, 1);
    store.Record(film_, 200.0, 0.0, 2);

    EXPECT_EQ(store.Find(film_)->duration, 1000.0);
    EXPECT_EQ(store.Find(film_)->position, 200.0);
}

TEST_F(WatchStateTest, ForgetRemovesRecord) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, 1);
    store.Forget(film_);

    EXPECT_FALSE(store.Find(film_).has_value());
    EXPECT_TRUE(store.Records().empty());
}

TEST_F(WatchStateTest, ForgetIsSilentForUnknownFile) {
    WatchStore store;
    EXPECT_NO_THROW(store.Forget(film_));
}

// The shelf is for things you're partway through, which is the whole reason
// the thresholds exist: these three cases are the policy.
TEST_F(WatchStateTest, ContinueWatchingExcludesBarelyStarted) {
    WatchStore store;
    store.Record(film_, 5.0, 1000.0, 1);  // 0.5%, below kStartedFraction

    EXPECT_TRUE(store.ContinueWatching().empty());
}

TEST_F(WatchStateTest, ContinueWatchingExcludesFinished) {
    WatchStore store;
    store.Record(film_, 990.0, 1000.0, 1);  // 99%, above kFinishedFraction

    EXPECT_TRUE(store.ContinueWatching().empty());
}

TEST_F(WatchStateTest, ContinueWatchingIncludesInProgress) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, 1);  // 30%

    ASSERT_EQ(store.ContinueWatching().size(), 1u);
    EXPECT_EQ(store.ContinueWatching().front().path, film_);
}

// Something was played; dropping it because the backend never reported a
// duration would silently lose it.
TEST_F(WatchStateTest, ContinueWatchingIncludesUnknownDuration) {
    WatchStore store;
    store.Record(film_, 300.0, 0.0, 1);

    EXPECT_EQ(store.ContinueWatching().size(), 1u);
}

TEST_F(WatchStateTest, ContinueWatchingIsMostRecentFirst) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, /*when=*/100);
    store.Record(other_, 300.0, 1000.0, /*when=*/200);

    std::vector<WatchRecord> shelf = store.ContinueWatching();
    ASSERT_EQ(shelf.size(), 2u);
    EXPECT_EQ(shelf[0].path, other_);  // newer
    EXPECT_EQ(shelf[1].path, film_);
}

// last_played only has second resolution, so ties are real. Path breaks them so
// the shelf doesn't reshuffle between launches.
TEST_F(WatchStateTest, ContinueWatchingOrderIsStableOnTimestampTie) {
    WatchStore store;
    store.Record(other_, 300.0, 1000.0, /*when=*/100);
    store.Record(film_, 300.0, 1000.0, /*when=*/100);

    std::vector<WatchRecord> shelf = store.ContinueWatching();
    ASSERT_EQ(shelf.size(), 2u);
    EXPECT_EQ(shelf[0].path, film_);  // "/media/film.mkv" < "/media/other.mkv"
    EXPECT_EQ(shelf[1].path, other_);
}

TEST_F(WatchStateTest, ResumePositionForInProgressFile) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, 1);

    EXPECT_EQ(store.ResumePosition(film_), 300.0);
}

TEST_F(WatchStateTest, ResumePositionIsZeroForUnknownFile) {
    WatchStore store;
    EXPECT_EQ(store.ResumePosition(film_), 0.0);
}

// A file watched to the end restarts from the top rather than parking the user
// on the credits.
TEST_F(WatchStateTest, ResumePositionIsZeroForFinishedFile) {
    WatchStore store;
    store.Record(film_, 990.0, 1000.0, 1);

    EXPECT_EQ(store.ResumePosition(film_), 0.0);
}

TEST_F(WatchStateTest, JsonRoundTripPreservesRecords) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, 100);
    store.Record(other_, 42.5, 500.0, 200);

    const fs::path path = dir_ / "watch.json";
    store.SaveToJson(path);

    WatchStore loaded = WatchStore::LoadFromJson(path);
    EXPECT_EQ(loaded.Records(), store.Records());
    EXPECT_EQ(loaded.ResumePosition(film_), 300.0);
}

// Save has to create its parent directory: on a first run nothing under
// $XDG_DATA_HOME/synaxis/ exists yet.
TEST_F(WatchStateTest, SaveCreatesMissingParentDirectories) {
    WatchStore store;
    store.Record(film_, 300.0, 1000.0, 1);

    const fs::path path = dir_ / "nested" / "deeper" / "watch.json";
    store.SaveToJson(path);

    EXPECT_TRUE(fs::exists(path));
}

// No watch state is the normal first-run condition, not an error.
TEST_F(WatchStateTest, LoadingMissingFileYieldsEmptyStore) {
    WatchStore loaded = WatchStore::LoadFromJson(dir_ / "does-not-exist.json");

    EXPECT_TRUE(loaded.Records().empty());
}

// A file that exists but is corrupt is a real problem, and unlike a missing
// one it should not be swallowed.
TEST_F(WatchStateTest, LoadingCorruptFileThrows) {
    const fs::path path = dir_ / "corrupt.json";
    {
        std::ofstream out(path);
        out << "{not valid json";
    }

    EXPECT_ANY_THROW(WatchStore::LoadFromJson(path));
}

TEST_F(WatchStateTest, DefaultPathHonorsXdgDataHome) {
    // Documented to sit beside library.json under $XDG_DATA_HOME/synaxis/.
    // Compared against MediaLibrary's own default rather than a literal, since
    // the two living in the same directory is the actual contract.
    const fs::path watch = WatchStore::DefaultPath();
    EXPECT_EQ(watch.filename(), "watch.json");
    EXPECT_EQ(watch.parent_path().filename(), "synaxis");
}

} // namespace
} // namespace synaxis
