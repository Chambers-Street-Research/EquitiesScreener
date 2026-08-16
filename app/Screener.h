#ifndef EQUITIESSCREENER_APP_SCREENER_H
#define EQUITIESSCREENER_APP_SCREENER_H

#include "../config/Config.h"
#include "../data/Equity.h"
#include "../engine/Engine.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace App {

    /// The two ways a run can be described: entirely by its caller
    /// (flag-based mode) or by a settings file the caller may override
    /// (settings-file mode). Both share this option set.
    struct Overrides {
        std::string                     inputFile;   // "" → use the settings file's `input`
        std::vector<Engine::FilterRule> filters;     // appended to every screen (AND-combined)
        std::optional<Config::SortSpec> sort;        // replaces each screen's own sort
        std::string                     outputFile;  // "" → each screen's own destination
    };

    /// One finished screen, ready for a front-end to report.
    struct ScreenResult {
        std::string                name;            // "" in flag-based mode
        std::vector<Data::Equity>  equities;        // filtered, then sorted
        std::size_t                universeSize{};  // rows the screen ran against
        std::optional<std::string> outputFile;      // set once written to disk
    };

    /// Progress callbacks, invoked in the order the work happens so a
    /// front-end can interleave them with its own output. Both are optional;
    /// an empty callback is skipped.
    ///
    /// `warning` messages carry no "Warning: " prefix and errors carry no
    /// "Error: " prefix - how they are labelled is the front-end's choice.
    struct Reporter {
        std::function<void(std::string_view)>    warning;
        std::function<void(const ScreenResult&)> screenFinished;
    };

    /// A run that could not complete. `exitCode` is the process status a CLI
    /// should return: 2 for a usage mistake, 1 for everything else.
    struct Error {
        std::string message;
        int         exitCode = 1;
    };

    /// Flag-based mode: a single implicit screen defined entirely by `opts`,
    /// which must name an input file. The result is written to
    /// `opts.outputFile` when one is set; otherwise it is only returned, and
    /// picking a destination is left to the caller.
    [[nodiscard]] std::expected<ScreenResult, Error>
    runFlagMode(const Overrides& opts, const Reporter& reporter = {});

    /// Settings-file mode: every `[screen:...]` in `settingsPath`, in file
    /// order, run against one shared universe and written to its resolved
    /// destination (`opts.outputFile` > the screen's `output =` >
    /// `<name>_screened.csv`).
    ///
    /// Unlike flag-based mode this excludes - and warns about - equities that
    /// are missing data a filter asks about, rather than treating the absent
    /// metric as zero.
    [[nodiscard]] std::expected<std::vector<ScreenResult>, Error>
    runConfigMode(std::string_view settingsPath,
                  const Overrides& opts,
                  const Reporter&  reporter = {});

} // namespace App

#endif // EQUITIESSCREENER_APP_SCREENER_H
