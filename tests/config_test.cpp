// Unit tests for the Config INI parser, written with GoogleTest.
// Dependency: gtest (declared in vcpkg.json, installed via the vcpkg CMake
// toolchain). See README for build/test instructions.
#include "config/Config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

    std::filesystem::path writeTemp(const char* name, const std::string& content) {
        auto path = std::filesystem::temp_directory_path() / name;
        std::ofstream out{path, std::ios::trunc};
        out << content;
        return path;
    }

    std::optional<Config::AppConfig> parseOk(const std::string& content, const char* name) {
        auto path = writeTemp(name, content);
        auto result = Config::parseSettingsFile(path.string());
        std::filesystem::remove(path);
        if (!result) return std::nullopt;
        return std::move(*result);
    }

    bool parseFails(const std::string& content, const char* name) {
        auto path = writeTemp(name, content);
        auto result = Config::parseSettingsFile(path.string());
        std::filesystem::remove(path);
        return !result;
    }

} // anonymous namespace

namespace {
    using namespace Config;
    using Data::Metric;
    using Engine::SortOrder;

    // ── happy path: global input + two screens ──
    TEST(ConfigParser, HappyPath) {
        auto cfg = parseOk(
            "input = stocks.csv\n"
            "[screen:value]\n"
            "filter = PE:0:30\n"
            "filter = ROE:0.15:\n"
            "sort = ROE:desc\n"
            "output = v.csv\n"
            "[screen:growth]\n"
            "filter = ROIC:0.25:\n",
            "cfg_happy.ini");
        ASSERT_TRUE(cfg.has_value());
        EXPECT_EQ(cfg->inputFile, "stocks.csv");
        ASSERT_EQ(cfg->screens.size(), 2u);

        const auto& v = cfg->screens[0];
        EXPECT_EQ(v.name, "value");
        ASSERT_EQ(v.filters.size(), 2u);
        EXPECT_EQ(v.filters[0].metric, Metric::PE);
        EXPECT_EQ(v.filters[0].min_val, 0.0f);
        EXPECT_EQ(v.filters[0].max_val, 30.0f);
        EXPECT_EQ(v.filters[1].metric, Metric::ROE);
        EXPECT_EQ(v.filters[1].min_val, 0.15f);
        EXPECT_EQ(v.filters[1].max_val, std::numeric_limits<float>::infinity());
        ASSERT_TRUE(v.sort.has_value());
        EXPECT_EQ(v.sort->metric, Metric::ROE);
        EXPECT_EQ(v.sort->order, SortOrder::Descending);
        ASSERT_TRUE(v.outputFile.has_value());
        EXPECT_EQ(*v.outputFile, "v.csv");

        const auto& g = cfg->screens[1];
        EXPECT_FALSE(g.sort.has_value());
        EXPECT_FALSE(g.outputFile.has_value());
    }

    // ── case-insensitivity: header, keys, metrics, orders ──
    TEST(ConfigParser, CaseInsensitive) {
        auto c2 = parseOk("[SCREEN:value]\nFILTER = pe:0:10\nSORT = Pb:asc\n", "cfg_case.ini");
        ASSERT_TRUE(c2.has_value());
        EXPECT_EQ(c2->screens[0].filters[0].metric, Metric::PE);
        ASSERT_TRUE(c2->screens[0].sort.has_value());
        EXPECT_EQ(c2->screens[0].sort->order, SortOrder::Ascending);
    }

    // ── whitespace around bound segments is tolerated ──
    TEST(ConfigParser, SpacedBoundSegments) {
        auto c3 = parseOk("[screen:s]\nfilter = PE: 0 :30\n", "cfg_space.ini");
        ASSERT_TRUE(c3.has_value());
        EXPECT_EQ(c3->screens[0].filters[0].min_val, 0.0f);
        EXPECT_EQ(c3->screens[0].filters[0].max_val, 30.0f);
    }

    // ── trailing garbage in a number is rejected (full-consume parse) ──
    TEST(ConfigParser, RejectsTrailingGarbage) {
        EXPECT_TRUE(parseFails("[screen:s]\nfilter = PE:0:30x\n", "cfg_garbage.ini"));
    }

    // ── structural errors (fail fast) ──
    TEST(ConfigParser, StructuralErrors) {
        EXPECT_TRUE(parseFails("[screen:a]\n[screen:A]\n", "cfg_dupscreen.ini"));          // duplicate screen name
        EXPECT_TRUE(parseFails("input = a\ninput = b\n[screen:s]\n", "cfg_dupinput.ini"));  // duplicate global input
        EXPECT_TRUE(parseFails("foo = 1\n[screen:s]\n", "cfg_unkglobal.ini"));             // unknown global key
        EXPECT_TRUE(parseFails("[filters]\n", "cfg_unksection.ini"));                      // unknown section
        EXPECT_TRUE(parseFails("[screen:s]\nfiltr = PE:0:30\n", "cfg_unkkey.ini"));        // unknown screen key
        EXPECT_TRUE(parseFails("[screen:s]\nfilter = PE:0\n", "cfg_malformed.ini"));       // malformed filter (2 parts)
        EXPECT_TRUE(parseFails("[screen:s]\nfilter = PE:abc:30\n", "cfg_badmin.ini"));     // non-numeric MIN
        EXPECT_TRUE(parseFails("[screen:s]\nsort = PE:sideways\n", "cfg_badsort.ini"));    // invalid sort order
        EXPECT_TRUE(parseFails("", "cfg_empty.ini"));                                      // empty file
        EXPECT_TRUE(parseFails("input = a\n", "cfg_noscreens.ini"));                       // no screens
        EXPECT_TRUE(parseFails("[screen:s]\ninput = a\n", "cfg_inputinscreen.ini"));       // input inside a screen
        EXPECT_TRUE(parseFails("[screen:s]\nsort = PE:asc\nsort = PE:desc\n", "cfg_dupsort.ini")); // duplicate sort
    }

    // ── CRLF endings, comments, blank lines ──
    TEST(ConfigParser, CrlfCommentsAndBlanks) {
        auto c6 = parseOk("# header comment\r\n; semicolon comment\r\n\r\ninput = stocks.csv\r\n"
                          "[screen:s]\r\nfilter = PE:0:30\r\n", "cfg_crlf.ini");
        ASSERT_TRUE(c6.has_value());
        EXPECT_EQ(c6->inputFile, "stocks.csv");
        EXPECT_EQ(c6->screens.size(), 1u);
    }

    // ── metric name helper ──
    TEST(ConfigParser, MetricNameParsing) {
        EXPECT_EQ(parseMetric("PE"), Metric::PE);
        EXPECT_EQ(parseMetric("pe"), Metric::PE);
        EXPECT_EQ(parseMetric("Roe"), Metric::ROE);
        EXPECT_FALSE(parseMetric("EPS").has_value());
    }

    // ── output naming ──
    TEST(ConfigParser, OutputNaming) {
        EXPECT_EQ(defaultOutputName("value"), "value_screened.csv");
        EXPECT_EQ(defaultOutputName("cheap large caps!"), "cheap_large_caps__screened.csv");
    }

} // namespace
