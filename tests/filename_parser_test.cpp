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

#include "synaxis/filename_parser.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace synaxis {
namespace {

TEST(FilenameParserTest, ParsesStandardEpisodeMarker) {
    ParsedFilename parsed = FilenameParser::parse("the.silent.orbit.s01e02.bluray.x264.mp4");

    EXPECT_EQ(parsed.season, 1);
    EXPECT_EQ(parsed.episode, 2);
    EXPECT_EQ(parsed.title, "the silent orbit");
    EXPECT_EQ(parsed.source, "bluray");
    EXPECT_FALSE(parsed.year.has_value());
}

TEST(FilenameParserTest, ParsesAlternateEpisodeMarker) {
    ParsedFilename parsed = FilenameParser::parse("Crimson.Static.2x04.WEB-DL.mkv");

    EXPECT_EQ(parsed.season, 2);
    EXPECT_EQ(parsed.episode, 4);
    EXPECT_EQ(parsed.title, "Crimson Static");
}

// The alternate NxNN pattern must not fire inside a codec token: "x264" has no
// digit before the 'x', which is exactly what the \b in kAltPattern defends.
TEST(FilenameParserTest, CodecTokenIsNotMistakenForEpisodeMarker) {
    ParsedFilename parsed = FilenameParser::parse("Some.Movie.1080p.x264.mkv");

    EXPECT_FALSE(parsed.season.has_value());
    EXPECT_FALSE(parsed.episode.has_value());
}

TEST(FilenameParserTest, ParsesMovieYear) {
    ParsedFilename parsed = FilenameParser::parse("Inception.2010.1080p.BluRay.mp4");

    EXPECT_EQ(parsed.year, 2010);
    EXPECT_EQ(parsed.title, "Inception");
    EXPECT_EQ(parsed.source, "BluRay");
}

TEST(FilenameParserTest, ParsesParenthesizedYear) {
    ParsedFilename parsed = FilenameParser::parse("Arrival (2016) 1080p.mkv");

    EXPECT_EQ(parsed.year, 2016);
    EXPECT_EQ(parsed.title, "Arrival");
}

TEST(FilenameParserTest, CollectsEditionTags) {
    ParsedFilename parsed = FilenameParser::parse("Dune.2021.PROPER.IMAX.HDR.AMZN.1080p.mkv");

    EXPECT_THAT(parsed.tags, ::testing::UnorderedElementsAre("PROPER", "IMAX", "HDR", "AMZN"));
    EXPECT_EQ(parsed.title, "Dune");
}

// Resolution is matched purely to find where the title ends, and deliberately
// never stored (see ParsedFilename). Without that match the title here would
// run on past "1080p" until the source tag.
TEST(FilenameParserTest, ResolutionEndsTitleButIsNotStored) {
    ParsedFilename parsed = FilenameParser::parse("The.Silent.Orbit.1080p.BluRay.mkv");

    EXPECT_EQ(parsed.title, "The Silent Orbit");
}

TEST(FilenameParserTest, StripsLeadingReleaseGroupTag) {
    ParsedFilename parsed = FilenameParser::parse("[SomeGroup] Cowboy.Bebop.S01E05.mkv");

    EXPECT_EQ(parsed.title, "Cowboy Bebop");
    EXPECT_EQ(parsed.season, 1);
    EXPECT_EQ(parsed.episode, 5);
}

// The leftmost marker wins: the season marker precedes the year, so the title
// stops at the season marker and the year is still recorded.
TEST(FilenameParserTest, LeftmostMarkerEndsTitle) {
    ParsedFilename parsed = FilenameParser::parse("Show.S02E03.2019.1080p.WEB-DL.mkv");

    EXPECT_EQ(parsed.title, "Show");
    EXPECT_EQ(parsed.season, 2);
    EXPECT_EQ(parsed.episode, 3);
    EXPECT_EQ(parsed.year, 2019);
}

// No marker of any kind: the title falls back to the stem, which is what keeps
// an unparseable file visible in the library rather than nameless.
TEST(FilenameParserTest, FallsBackToStemWhenNoMarkers) {
    ParsedFilename parsed = FilenameParser::parse("home_video_clip.mp4");

    EXPECT_EQ(parsed.title, "home video clip");
    EXPECT_FALSE(parsed.season.has_value());
    EXPECT_FALSE(parsed.year.has_value());
}

TEST(FilenameParserTest, LeavesTitleUnsetWhenNothingSurvives) {
    ParsedFilename parsed = FilenameParser::parse("S01E01.mkv");

    EXPECT_FALSE(parsed.title.has_value());
    EXPECT_EQ(parsed.season, 1);
    EXPECT_EQ(parsed.episode, 1);
}

// Source matching is case-insensitive, and the filename's own spelling is what
// gets stored rather than a normalized form.
TEST(FilenameParserTest, SourceMatchIsCaseInsensitive) {
    EXPECT_EQ(FilenameParser::parse("A.Film.2020.webrip.mkv").source, "webrip");
    EXPECT_EQ(FilenameParser::parse("A.Film.2020.WEBRip.mkv").source, "WEBRip");
}

} // namespace
} // namespace synaxis
