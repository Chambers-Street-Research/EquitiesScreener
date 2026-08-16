// ============================================================
//  EquitiesScreener - command-line front-end
//
//  This file is the CLI and nothing else: it parses argv, points
//  equities_core at a settings file, and formats what comes back for the
//  terminal. All screening logic lives in App::runFlagMode /
//  App::runConfigMode (app/Screener.h).
// ============================================================

#include "app/Screener.h"
#include "config/Config.h"
#include "engine/Engine.h"
#include "io/Csv.h"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// ============================================================
//  CLI argument representation
// ============================================================

struct CliArgs {
    std::string                     inputFile;   // positional or --input
    std::optional<std::string>      configFile;  // --config
    std::vector<Engine::FilterRule> filters;     // -f (CLI extras)
    std::optional<Config::SortSpec> sort;        // -s
    std::string                     outputFile;  // -o
    bool pretty = false;
    bool help   = false;
    bool valid  = true;
};

// ============================================================
//  CLI helpers
// ============================================================

namespace {

    void printUsage() {
        std::println(stderr,
            "EquitiesScreener - filter and rank stocks from a CSV file.\n"
            "\n"
            "Usage:\n"
            "  EquitiesScreener <input.csv> [options]          flag-based mode\n"
            "  EquitiesScreener [--config <settings.ini>] [options]\n"
            "                                                  settings-file mode\n"
            "                                                  (auto-loads ./screener.ini\n"
            "                                                   when no input/config given)\n"
            "\n"
            "Settings-file mode:\n"
            "  -c, --config <FILE>        Use FILE as the settings file.\n"
            "  -i, --input <FILE>         Override the universe CSV declared in the file.\n"
            "\n"
            "Options (flag-based mode; in settings-file mode they extend every screen):\n"
            "  -f, --filter <METRIC:MIN:MAX>   Apply a filter. MAX is optional.\n"
            "                                  Repeatable. Examples:\n"
            "                                    -f PE:0:30       (0 <= PE <= 30)\n"
            "                                    -f ROE:0.15:     (ROE >= 15%)\n"
            "                                    -f EVEBITDA::20  (EV/EBITDA <= 20)\n"
            "\n"
            "  -s, --sort   <METRIC[:ASC]>     Sort by metric (default: descending).\n"
            "                                  Example: -s ROE  or  -s PE:ASC\n"
            "\n"
            "  -o, --output <FILE>             Write CSV to file. Default: stdout.\n"
            "                                  In settings-file mode only valid with ONE screen.\n"
            "\n"
            "  -p, --pretty                    Pretty-print results to terminal.\n"
            "\n"
            "  -h, --help                      Show this message.\n"
        );
    }

    std::optional<Engine::FilterRule> parseFilterArg(std::string_view arg) {
        auto colon1 = arg.find(':');
        if (colon1 == std::string_view::npos) return std::nullopt;

        std::string_view metricName = arg.substr(0, colon1);
        auto metric = Config::parseMetric(metricName);
        if (!metric) {
            std::println(stderr, "Unknown metric: \"{}\"", metricName);
            return std::nullopt;
        }

        auto rest   = arg.substr(colon1 + 1);
        auto colon2 = rest.find(':');
        auto minStr = rest.substr(0, colon2);
        auto maxStr = (colon2 == std::string_view::npos)
                          ? std::string_view{}
                          : rest.substr(colon2 + 1);

        // trim whitespace before numeric parsing
        auto trimSv = [](std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
            return s;
        };

        float minVal = -std::numeric_limits<float>::infinity();
        float maxVal =  std::numeric_limits<float>::infinity();

        if (!minStr.empty()) {
            auto trimmed = trimSv(minStr);
            auto [_, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), minVal);
            if (ec != std::errc{}) {
                std::println(stderr, "Invalid number in filter: \"{}\"", trimmed);
                return std::nullopt;
            }
        }
        if (!maxStr.empty()) {
            auto trimmed = trimSv(maxStr);
            auto [_, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), maxVal);
            if (ec != std::errc{}) {
                std::println(stderr, "Invalid number in filter: \"{}\"", trimmed);
                return std::nullopt;
            }
        }

        return Engine::FilterRule{*metric, minVal, maxVal};
    }

    std::optional<Config::SortSpec> parseSortArg(std::string_view arg) {
        auto colon = arg.find(':');
        std::string_view metricName = arg.substr(0, colon);
        auto metric = Config::parseMetric(metricName);
        if (!metric) {
            std::println(stderr, "Unknown sort metric: \"{}\"", metricName);
            return std::nullopt;
        }
        Engine::SortOrder order = Engine::SortOrder::Descending;
        if (colon != std::string_view::npos) {
            auto orderStr = arg.substr(colon + 1);
            if (orderStr == "ASC" || orderStr == "asc")
                order = Engine::SortOrder::Ascending;
        }
        return Config::SortSpec{*metric, order};
    }

    // ============================================================
    //  Console output
    // ============================================================

    /// Options the core needs, minus everything that is purely presentational.
    App::Overrides toOverrides(const CliArgs& opts) {
        return App::Overrides{
            .inputFile  = opts.inputFile,
            .filters    = opts.filters,
            .sort       = opts.sort,
            .outputFile = opts.outputFile,
        };
    }

    int reportError(const App::Error& error) {
        std::println(stderr, "Error: {}", error.message);
        return error.exitCode;
    }

    void printWarning(std::string_view message) {
        std::println(stderr, "Warning: {}", message);
    }

    // ── flag-based mode: input CSV plus -f/-s/-o/-p ──
    int runFlagMode(const CliArgs& opts) {
        App::Overrides overrides = toOverrides(opts);

        // --pretty replaces file output entirely, so the core is not asked to
        // write anything and the results come back for the terminal instead.
        if (opts.pretty) overrides.outputFile.clear();

        auto screen = App::runFlagMode(overrides, App::Reporter{.warning = printWarning});
        if (!screen) return reportError(screen.error());

        if (screen->outputFile) {
            std::println(stderr, "Wrote {} equities to \"{}\"",
                         screen->equities.size(), *screen->outputFile);
        } else if (opts.pretty) {
            Engine::Engine::printResults(screen->equities, screen->universeSize);
        } else if (!IO::writeCsv(std::cout, screen->equities)) {
            std::println(stderr, "Error: could not write to stdout");
            return 1;
        }
        return 0;
    }

    // ── settings-file mode: every screen in the file ──
    int runConfigMode(const std::string& settingsPath, const CliArgs& opts) {
        // Reported as each screen finishes so the output stays in step with
        // the warnings the screen produced.
        auto announce = [&opts](const App::ScreenResult& screen) {
            std::println(stderr, "Screen '{}': wrote {} equities to \"{}\"",
                         screen.name, screen.equities.size(),
                         screen.outputFile.value_or(std::string{}));
            if (opts.pretty)
                Engine::Engine::printResults(screen.equities, screen.universeSize);
        };

        auto screens = App::runConfigMode(settingsPath, toOverrides(opts),
                                          App::Reporter{.warning        = printWarning,
                                                        .screenFinished = announce});
        if (!screens) return reportError(screens.error());
        return 0;
    }

} // anonymous namespace

// ============================================================
//  CLI argument parser
// ============================================================

CliArgs parseArgs(int argc, char** argv) {
    CliArgs opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        if (arg == "-h" || arg == "--help") {
            opts.help = true;
            return opts;
        }
        if (arg == "-p" || arg == "--pretty") {
            opts.pretty = true;
            continue;
        }
        if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) { opts.valid = false; return opts; }
            opts.configFile = argv[++i];
            continue;
        }
        if (arg == "-i" || arg == "--input") {
            if (i + 1 >= argc) { opts.valid = false; return opts; }
            opts.inputFile = argv[++i];
            continue;
        }
        if (arg == "-f" || arg == "--filter") {
            if (i + 1 >= argc) { opts.valid = false; return opts; }
            auto rule = parseFilterArg(argv[++i]);
            if (!rule) { opts.valid = false; return opts; }
            opts.filters.push_back(std::move(*rule));
            continue;
        }
        if (arg == "-s" || arg == "--sort") {
            if (i + 1 >= argc) { opts.valid = false; return opts; }
            auto s = parseSortArg(argv[++i]);
            if (!s) { opts.valid = false; return opts; }
            opts.sort = std::move(*s);
            continue;
        }
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) { opts.valid = false; return opts; }
            opts.outputFile = argv[++i];
            continue;
        }

        if (!arg.starts_with('-')) {
            if (opts.inputFile.empty()) {
                opts.inputFile = arg;
            } else {
                opts.valid = false;   // multiple positional arguments
                return opts;
            }
        } else {
            opts.valid = false;       // unknown flag
            return opts;
        }
    }
    return opts;
}

// ============================================================
//  Main — mode dispatch
// ============================================================

int main(int argc, char* argv[]) {
    auto opts = parseArgs(argc, argv);

    if (opts.help) {
        printUsage();
        return 0;
    }
    if (!opts.valid) {
        printUsage();
        return 2;
    }

    if (opts.configFile) return runConfigMode(*opts.configFile, opts);
    if (!opts.inputFile.empty()) return runFlagMode(opts);

    // no --config and no input: auto-load ./screener.ini if present
    if (std::filesystem::exists("screener.ini")) return runConfigMode("screener.ini", opts);

    printUsage();
    return 2;
}
