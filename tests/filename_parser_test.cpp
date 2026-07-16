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

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "synaxis/filename_parser.hpp"

namespace {

using synaxis::FilenameParser;
using synaxis::ParsedFilename;
using nlohmann::json;

struct Case {
    std::string path;      // relative to fixtures/media_library, for diagnostics
    std::string filename;  // basename only — what the parser actually receives
    ParsedFilename expected;
};

std::optional<std::string> ToOptionalString(const json& j) {
    if (j.is_null()) return std::nullopt;
    return j.get<std::string>();
}

std::optional<int> ToOptionalInt(const json& j) {
    if (j.is_null()) return std::nullopt;
    return j.get<int>();
}

std::vector<Case> LoadCases() {
    std::ifstream in(std::string(FIXTURES_DIR) + "/expected_metadata.json");
    json manifest;
    in >> manifest;

    std::vector<Case> cases;
    for (const auto& entry : manifest.at("entries")) {
        Case c;
        c.path = entry.at("path").get<std::string>();
        auto slash = c.path.find_last_of('/');
        c.filename = slash == std::string::npos ? c.path : c.path.substr(slash + 1);

        // resolution/codec in the manifest are for the later mpv-based
        // inspection step, not asserted here.
        c.expected.season = ToOptionalInt(entry.at("season"));
        c.expected.episode = ToOptionalInt(entry.at("episode"));
        c.expected.year = ToOptionalInt(entry.at("year"));
        c.expected.source = ToOptionalString(entry.at("source"));
        c.expected.title = ToOptionalString(entry.at("title"));
        c.expected.tags = entry.at("tags").get<std::vector<std::string>>();

        cases.push_back(std::move(c));
    }
    return cases;
}

void PrintTo(const Case& c, std::ostream* os) {
    *os << c.filename;
}

std::string SanitizeTestName(const std::string& name) {
    std::string sanitized;
    for (char c : name) {
        sanitized += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    return sanitized;
}

class FilenameParserTest : public ::testing::TestWithParam<Case> {};

TEST_P(FilenameParserTest, ExtractsExpectedFields) {
    const Case& c = GetParam();
    ParsedFilename actual = FilenameParser::parse(c.filename);

    EXPECT_EQ(actual.season, c.expected.season) << c.filename;
    EXPECT_EQ(actual.episode, c.expected.episode) << c.filename;
    EXPECT_EQ(actual.year, c.expected.year) << c.filename;
    EXPECT_EQ(actual.source, c.expected.source) << c.filename;
    EXPECT_EQ(actual.title, c.expected.title) << c.filename;
    EXPECT_EQ(actual.tags, c.expected.tags) << c.filename;
}

INSTANTIATE_TEST_SUITE_P(
    MediaLibraryFixtures,
    FilenameParserTest,
    ::testing::ValuesIn(LoadCases()),
    [](const ::testing::TestParamInfo<Case>& info) {
        return SanitizeTestName(info.param.filename);
    });

}  // namespace
