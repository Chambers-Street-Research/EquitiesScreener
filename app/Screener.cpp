#include "Screener.h"

#include "../io/Csv.h"

#include <format>
#include <utility>

namespace App {

    // ────────────────────────────────────────────────
    //  Internal helpers
    // ────────────────────────────────────────────────

    namespace {

        /// Invoke an optional Reporter callback, if the front-end supplied one.
        template <typename Callback, typename... Args>
        void notify(const Callback& callback, Args&&... args) {
            if (callback) callback(std::forward<Args>(args)...);
        }

        /// Turn a fatal CSV read failure into a front-end-agnostic message.
        Error csvReadError(IO::CsvError error, std::string_view path) {
            switch (error) {
            case IO::CsvError::FileNotFound:
                return {std::format("file not found - \"{}\"", path), 1};
            case IO::CsvError::EmptyFile:
                return {std::format("file is empty - \"{}\"", path), 1};
            default:
                return {std::format("could not read \"{}\"", path), 1};
            }
        }

        /// Read the universe CSV, forwarding malformed-row warnings as they
        /// are discovered.
        std::expected<std::vector<Data::Equity>, Error>
        loadUniverse(std::string_view path, const Reporter& reporter) {
            auto csv = IO::readCsv(path);
            if (!csv) return std::unexpected(csvReadError(csv.error(), path));

            for (const auto& warning : csv->warnings)
                notify(reporter.warning, warning);

            return std::move(csv->equities);
        }

        /// Write a finished screen to disk and record where it landed.
        std::expected<void, Error> writeScreen(ScreenResult& screen, std::string_view path) {
            if (!IO::writeCsv(path, screen.equities))
                return std::unexpected(Error{std::format("could not write to \"{}\"", path), 1});

            screen.outputFile = std::string{path};
            return {};
        }

    } // anonymous namespace

    // ────────────────────────────────────────────────
    //  Flag-based mode
    // ────────────────────────────────────────────────

    std::expected<ScreenResult, Error>
    runFlagMode(const Overrides& opts, const Reporter& reporter) {
        auto universe = loadUniverse(opts.inputFile, reporter);
        if (!universe) return std::unexpected(universe.error());

        Engine::Engine screener(std::move(*universe));
        for (const auto& rule : opts.filters)
            screener.addFilter(rule);

        ScreenResult screen{
            .name         = {},
            .equities     = screener.runScreen(),
            .universeSize = screener.getUniverseSize(),
            .outputFile   = std::nullopt,
        };

        if (opts.sort)
            Engine::Engine::sortEquities(screen.equities, opts.sort->metric, opts.sort->order);

        if (!opts.outputFile.empty()) {
            auto written = writeScreen(screen, opts.outputFile);
            if (!written) return std::unexpected(written.error());
        }

        notify(reporter.screenFinished, screen);
        return screen;
    }

    // ────────────────────────────────────────────────
    //  Settings-file mode
    // ────────────────────────────────────────────────

    std::expected<std::vector<ScreenResult>, Error>
    runConfigMode(std::string_view settingsPath, const Overrides& opts, const Reporter& reporter) {
        auto parsed = Config::parseSettingsFile(settingsPath);
        if (!parsed) return std::unexpected(Error{std::move(parsed.error()), 1});

        Config::AppConfig config = std::move(*parsed);

        // resolve input CSV: caller override (positional or --input) > settings input
        std::string inputPath = opts.inputFile.empty() ? config.inputFile : opts.inputFile;
        if (inputPath.empty()) {
            return std::unexpected(Error{std::format(
                "no input CSV - set \"input =\" in \"{}\" or pass one on the command line",
                settingsPath), 1});
        }

        if (!opts.outputFile.empty() && config.screens.size() != 1) {
            return std::unexpected(Error{
                "--output is only valid when the settings file defines exactly ONE screen "
                "(use per-screen \"output =\" for multiple screens)", 2});
        }

        auto universe = loadUniverse(inputPath, reporter);
        if (!universe) return std::unexpected(universe.error());

        // the universe is read once and shared by every screen
        Engine::Engine screener(std::move(*universe));

        std::vector<ScreenResult> results;
        results.reserve(config.screens.size());

        for (const auto& config_screen : config.screens) {
            // 1) filters: settings first, caller extras appended (AND-combined)
            std::vector<Engine::FilterRule> rules = config_screen.filters;
            rules.insert(rules.end(), opts.filters.begin(), opts.filters.end());

            // 2) presence-aware screening: exclude + warn on missing metric data
            ScreenResult screen{
                .name         = config_screen.name,
                .equities     = {},
                .universeSize = screener.getUniverseSize(),
                .outputFile   = std::nullopt,
            };
            for (const auto& equity : screener.getUniverse()) {
                bool missing = false;
                for (const auto& rule : rules) {
                    if (!equity.hasMetric(rule.metric)) {
                        notify(reporter.warning, std::format(
                            "Screen '{}': excluded {} - missing {} data",
                            config_screen.name, equity.getTicker(), Config::metricName(rule.metric)));
                        missing = true;   // report every missing metric (no break)
                    }
                }
                if (missing) continue;
                if (screener.matchesFilters(equity, rules)) screen.equities.push_back(equity);
            }

            // 3) sort: a caller-supplied sort overrides the screen's own
            std::optional<Config::SortSpec> sortSpec = opts.sort ? opts.sort : config_screen.sort;
            if (sortSpec)
                Engine::Engine::sortEquities(screen.equities, sortSpec->metric, sortSpec->order);

            // 4) output path: override > output= > {name}_screened.csv
            std::string outPath = opts.outputFile.empty()
                ? (config_screen.outputFile ? *config_screen.outputFile
                                            : Config::defaultOutputName(config_screen.name))
                : opts.outputFile;

            auto written = writeScreen(screen, outPath);
            if (!written) return std::unexpected(written.error());

            notify(reporter.screenFinished, screen);
            results.push_back(std::move(screen));
        }

        return results;
    }

} // namespace App
