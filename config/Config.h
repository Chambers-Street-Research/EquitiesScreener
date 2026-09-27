#ifndef EQUITIESSCREENER_CONFIG_H
#define EQUITIESSCREENER_CONFIG_H

#include "../data/Equity.h"
#include "../io/MarketData.h"
#include "../engine/Engine.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Config {

    /// A per-screen sort specification from a `sort =` line.
    struct SortSpec {
        Data::Metric      metric;
        Engine::SortOrder order{Engine::SortOrder::Descending};
    };

    /// One `[screen:NAME]` section parsed from a settings file.
    struct ScreenConfig {
        std::string                       name;        // from [screen:NAME]
        std::vector<Engine::FilterRule>   filters;     // in file order, AND-combined
        std::optional<SortSpec>           sort;        // nullopt → keep input order
        std::optional<std::string>        outputFile;  // nullopt → defaultOutputName()
    };

    /// FMP configuration from an `[fmp]` section.
    struct FmpConfig {
        std::string              apiKey;
        std::string              baseUrl{"https://financialmodelingprep.com/api/v3"};
        std::vector<std::string> symbols;
        std::string              sector;
        std::size_t              limit{25};
        bool                     enabled{false};
    };

    struct AlpacaConfig {
        IO::AlpacaOptions options;
        std::vector<std::string> symbols;
    };

    /// The full parsed contents of a settings file.
    struct AppConfig {
        std::string                       inputFile;   // global `input =` key (may be empty)
        std::string                       source;      // "csv", "fmp", or "alpaca"
        std::optional<AlpacaConfig> alpaca;
        std::optional<FmpConfig>          fmp;         // parsed [fmp] section
        std::vector<ScreenConfig>         screens;     // in file order
    };

    // ────────────────────────────────────────────────
    //  Public API
    // ────────────────────────────────────────────────

    /// Parse a metric name case-insensitively ("PE", "pe", "Roe").
    /// Returns nullopt for unknown names.
    [[nodiscard]] std::optional<Data::Metric> parseMetric(std::string_view name) noexcept;

    /// Canonical metric name for user-facing messages ("PE", "ROE", ...).
    [[nodiscard]] std::string_view metricName(Data::Metric metric) noexcept;

    /// Parse an INI settings file. Fails fast on the first error; the returned
    /// error string includes the file path and line number.
    ///
    /// Grammar (see file-settings-spec.md):
    ///   - whole-line comments (`#` or `;`) and blank lines are ignored
    ///   - global keys before any section: `input = <path.csv>` (at most once)
    ///   - sections: `[screen:NAME]` (prefix and metric names case-insensitive)
    ///   - screen keys: `filter = METRIC:MIN:MAX` (repeatable, AND-combined),
    ///     `sort = METRIC[:ASC|DESC]` (at most once), `output = <file.csv>`
    ///   - repeated scalar keys and duplicate screen names are errors
    [[nodiscard]] std::expected<AppConfig, std::string>
    parseSettingsFile(std::string_view filepath);

    /// Auto-generated output file name for a screen: "value" → "value_screened.csv".
    /// Characters outside [A-Za-z0-9_-] are replaced with '_'.
    [[nodiscard]] std::string defaultOutputName(std::string_view screenName);

} // namespace Config

#endif // EQUITIESSCREENER_CONFIG_H
