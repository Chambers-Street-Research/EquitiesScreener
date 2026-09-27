#include <charconv>
#include "Config.h"

#include <charconv>
#include <cctype>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <system_error>

namespace Config {

    // ────────────────────────────────────────────────
    //  Internal helpers
    // ────────────────────────────────────────────────

    namespace {

        /// Trim leading and trailing whitespace from a string_view (in-place).
        constexpr auto trim(std::string_view sv) noexcept -> std::string_view {
            auto start = sv.begin();
            auto end   = sv.end();
            while (start != end && std::isspace(static_cast<unsigned char>(*start))) ++start;
            while (start != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
            return {start, end};
        }

        /// Uppercase copy (ASCII).
        std::string toUpper(std::string_view sv) {
            std::string out;
            out.reserve(sv.size());
            for (char c : sv)
                out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return out;
        }

        std::string toLower(std::string_view sv) {
            std::string out;
            out.reserve(sv.size());
            for (char c : sv)
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        /// Parse a float from a string_view, consuming the ENTIRE input
        /// (no trailing garbage). Empty string → nullopt.
        auto parseFloatFull(std::string_view sv) noexcept -> std::optional<float> {
            if (sv.empty()) return std::nullopt;
            float val{};
            auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
            if (ec != std::errc{} || ptr != sv.data() + sv.size()) return std::nullopt;
            return val;
        }

        auto splitFields(std::string_view input, char delimiter)
    -> std::vector<std::string_view> {
            std::vector<std::string_view> fields;

            std::size_t start = 0;
            while (start <= input.size()) {
                const std::size_t separator = input.find(delimiter, start);

                if (separator == std::string_view::npos) {
                    fields.emplace_back(input.substr(start));
                    break;
                }

                fields.emplace_back(input.substr(start, separator - start));
                start = separator + 1;
            }

            return fields;
        }

        /// Parse a `filter = METRIC:MIN:MAX` value into a FilterRule.
        auto parseFilterLine(std::string_view value, size_t line, std::string_view path)
            -> std::expected<Engine::FilterRule, std::string> {

            auto parts = splitFields(value, ':');
            if (parts.size() != 3) {
                return std::unexpected(std::format(
                    "{}:{}: invalid filter \"{}\" (expected METRIC:MIN:MAX)", path, line, value));
            }

            auto metric = parseMetric(parts[0]);
            if (!metric) {
                return std::unexpected(std::format(
                    "{}:{}: unknown metric \"{}\" in filter", path, line, trim(parts[0])));
            }

            float minVal = -std::numeric_limits<float>::infinity();
            float maxVal =  std::numeric_limits<float>::infinity();

            // trim each bound segment individually (consistent with the CLI -f parser)
            auto minSeg = trim(parts[1]);
            auto maxSeg = trim(parts[2]);
            if (!minSeg.empty()) {
                auto v = parseFloatFull(minSeg);
                if (!v) return std::unexpected(std::format(
                    "{}:{}: invalid MIN \"{}\" in filter", path, line, minSeg));
                minVal = *v;
            }
            if (!maxSeg.empty()) {
                auto v = parseFloatFull(maxSeg);
                if (!v) return std::unexpected(std::format(
                    "{}:{}: invalid MAX \"{}\" in filter", path, line, maxSeg));
                maxVal = *v;
            }

            return Engine::FilterRule{*metric, minVal, maxVal};
        }

        /// Parse a `sort = METRIC[:ASC|DESC]` value into a SortSpec.
        auto parseSortLine(std::string_view value, size_t line, std::string_view path)
            -> std::expected<SortSpec, std::string> {

            auto colon  = value.find(':');
            auto metric = parseMetric(trim(value.substr(0, colon)));
            if (!metric) {
                return std::unexpected(std::format(
                    "{}:{}: unknown sort metric \"{}\"", path, line, trim(value.substr(0, colon))));
            }

            SortSpec spec{*metric, Engine::SortOrder::Descending};
            if (colon != std::string_view::npos) {
                auto order = toUpper(trim(value.substr(colon + 1)));
                if (order == "ASC") {
                    spec.order = Engine::SortOrder::Ascending;
                } else if (order == "DESC") {
                    spec.order = Engine::SortOrder::Descending;
                } else {
                    return std::unexpected(std::format(
                        "{}:{}: invalid sort order \"{}\" (expected ASC or DESC)",
                        path, line, trim(value.substr(colon + 1))));
                }
            }
            return spec;
        }

    } // anonymous namespace

    // ────────────────────────────────────────────────
    //  Public API implementations
    // ────────────────────────────────────────────────

    std::optional<Data::Metric> parseMetric(std::string_view name) noexcept {
        static const std::map<std::string, Data::Metric> map = {
            {"SPOTPRICE", Data::Metric::SpotPrice},
            {"PE",        Data::Metric::PE},
            {"FORWARDPE", Data::Metric::ForwardPE},
            {"PB",        Data::Metric::PB},
            {"PS",        Data::Metric::PS},
            {"EVEBITDA",  Data::Metric::EVEBITDA},
            {"ROA",       Data::Metric::ROA},
            {"ROE",       Data::Metric::ROE},
            {"ROIC",      Data::Metric::ROIC},
        };
        auto it = map.find(toUpper(trim(name)));
        if (it == map.end()) return std::nullopt;
        return it->second;
    }

    std::string_view metricName(Data::Metric metric) noexcept {
        switch (metric) {
            case Data::Metric::SpotPrice: return "SPOTPRICE";
            case Data::Metric::PE:        return "PE";
            case Data::Metric::ForwardPE: return "FORWARDPE";
            case Data::Metric::PB:        return "PB";
            case Data::Metric::PS:        return "PS";
            case Data::Metric::EVEBITDA:  return "EVEBITDA";
            case Data::Metric::ROA:       return "ROA";
            case Data::Metric::ROE:       return "ROE";
            case Data::Metric::ROIC:      return "ROIC";
        }
        return "?";
    }

    std::string defaultOutputName(std::string_view screenName) {
        std::string safe;
        safe.reserve(screenName.size() + 12);
        for (char c : screenName) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                         || (c >= '0' && c <= '9') || c == '_' || c == '-';
            safe += ok ? c : '_';
        }
        return std::format("{}_screened.csv", safe);
    }

    std::expected<AppConfig, std::string> parseSettingsFile(std::string_view filepath) {
        std::ifstream file{std::string{filepath}};
        if (!file.is_open()) {
            return std::unexpected(std::format("cannot open settings file \"{}\"", filepath));
        }

        AppConfig config;
        std::optional<ScreenConfig> current;   // engaged while inside a [screen:...]
        bool inAlpaca = false;
        std::set<std::string> seenInAlpaca;
        bool inFmp = false;
        bool seenFmp = false;
        std::set<std::string> seenScreens;     // lowercase screen names
        std::set<std::string> seenGlobal;      // lowercase global keys
        std::set<std::string> seenInScreen;    // scalar keys in the current screen
        std::set<std::string> seenInFmp;       // scalar keys in [fmp]

        auto err = [&](size_t line, std::string_view message) {
            return std::unexpected(std::format("{}:{}: {}", filepath, line, message));
        };

        std::string raw;
        size_t lineNum = 0;
        while (std::getline(file, raw)) {
            ++lineNum;
            auto line = trim(raw);
            if (line.empty()) continue;
            if (line.front() == '#' || line.front() == ';') continue;  // whole-line comment

            // ── section header: [screen:NAME] or [fmp] ──
            if (line.front() == '[') {
                if (line.back() != ']') {
                    return err(lineNum, std::format(
                        "malformed section header \"{}\" (expected [screen:NAME] or [fmp])", line));
                }
                auto inner      = line.substr(1, line.size() - 2);
                auto upperInner = toUpper(inner);

                inAlpaca = false;
                if (upperInner == "ALPACA") {
                    if (config.alpaca) return err(lineNum, "duplicate [alpaca] section");
                    if (current) { config.screens.push_back(std::move(*current)); current.reset(); }
                    inAlpaca = true; inFmp = false;
                    config.alpaca.emplace();
                    continue;
                }
                if (upperInner == "FMP") {
                    if (seenFmp) {
                        return err(lineNum, "duplicate [fmp] section");
                    }
                    seenFmp = true;
                    if (current) {
                        config.screens.push_back(std::move(*current));
                        current.reset();
                    }
                    inFmp = true;
                    Config::FmpConfig fmpCfg;
                    fmpCfg.enabled = true;
                    config.fmp = std::move(fmpCfg);
                    seenInFmp.clear();
                    continue;
                }

                if (upperInner.size() < 7 || upperInner.substr(0, 7) != "SCREEN:") {
                    return err(lineNum, std::format(
                        "unknown section \"[{}]\" (expected [screen:NAME])", inner));
                }
                auto name = trim(inner.substr(7));
                if (name.empty()) return err(lineNum, "empty screen name");

                auto key = toUpper(name);
                if (!seenScreens.insert(key).second) {
                    return err(lineNum, std::format(
                        "duplicate screen \"{}\" (names are case-insensitive)", name));
                }

                if (current) {
                    config.screens.push_back(std::move(*current));
                    current.reset();
                }
                inFmp = false;
                current       = ScreenConfig{std::string{name}, {}, std::nullopt, std::nullopt};
                seenInScreen.clear();
                continue;
            }

            // ── key = value ──
            auto eq = line.find('=');
            if (eq == std::string_view::npos) {
                return err(lineNum, std::format(
                    "malformed line \"{}\" (expected key = value)", line));
            }
            auto key   = trim(line.substr(0, eq));
            auto value = trim(line.substr(eq + 1));
            if (key.empty()) return err(lineNum, "empty key before '='");
            auto upperKey = toUpper(key);

            // ── global scope (before any section) ──
            if (inAlpaca) {
                if (!seenInAlpaca.insert(upperKey).second) return err(lineNum, "duplicate [alpaca] key");
                auto& cfg = *config.alpaca;
                if (upperKey == "API_KEY") cfg.options.apiKey = value;
                else if (upperKey == "SECRET_KEY") cfg.options.secretKey = value;
                else if (upperKey == "BASE_URL") cfg.options.baseUrl = value;
                else if (upperKey == "FEED") {
                    if (value != "iex" && value != "sip") return err(lineNum, "feed must be iex or sip");
                    cfg.options.feed = value;
                } else if (upperKey == "TIMEOUT_MS") {
                    long timeout=0;
                    auto [p,e]=std::from_chars(value.data(),value.data()+value.size(),timeout);
                    if (e!=std::errc{} || p!=value.data()+value.size() || timeout<1 || timeout>60000)
                        return err(lineNum, "timeout_ms must be an integer in 1..60000");
                    cfg.options.timeoutMs=timeout;
                } else if (upperKey == "SYMBOLS") {
                    for (auto sym : splitFields(value, ',')) {
                        auto s=trim(sym);
                        if (s.empty()) return err(lineNum, "empty Alpaca symbol");
                        cfg.symbols.emplace_back(s);
                    }
                } else return err(lineNum, "unknown [alpaca] key");
                continue;
            }
            if (!current && !inFmp) {
                if (upperKey == "INPUT") {
                    if (!seenGlobal.insert(upperKey).second) {
                        return err(lineNum, "duplicate global key \"input\"");
                    }
                    config.inputFile = std::string{value};
                    continue;
                }
                if (upperKey == "SOURCE") {
                    if (!seenGlobal.insert(upperKey).second) {
                        return err(lineNum, "duplicate global key \"source\"");
                    }
                    auto src = toLower(value);
                    if (src != "csv" && src != "fmp" && src != "alpaca") {
                        return err(lineNum, std::format("invalid source \"{}\" (expected 'csv', 'fmp', or 'alpaca')", value));
                    }
                    config.source = src;
                    continue;
                }
                return err(lineNum, std::format(
                    "unknown global key \"{}\" (only \"input\" and \"source\" are allowed before sections)", key));
            }

            // ── inside [fmp] section ──
            if (inFmp) {
                if (upperKey == "API_KEY") {
                    if (!seenInFmp.insert(upperKey).second) {
                        return err(lineNum, "duplicate \"api_key\" in [fmp]");
                    }
                    config.fmp->apiKey = std::string{value};
                } else if (upperKey == "BASE_URL") {
                    if (!seenInFmp.insert(upperKey).second) {
                        return err(lineNum, "duplicate \"base_url\" in [fmp]");
                    }
                    config.fmp->baseUrl = std::string{value};
                } else if (upperKey == "SECTOR") {
                    if (!seenInFmp.insert(upperKey).second) {
                        return err(lineNum, "duplicate \"sector\" in [fmp]");
                    }
                    config.fmp->sector = std::string{value};
                } else if (upperKey == "LIMIT") {
                    if (!seenInFmp.insert(upperKey).second) {
                        return err(lineNum, "duplicate \"limit\" in [fmp]");
                    }
                    auto parsed = parseFloatFull(value);
                    if (!parsed || *parsed <= 0.0f) {
                        return err(lineNum, std::format("invalid limit \"{}\" in [fmp]", value));
                    }
                    config.fmp->limit = static_cast<std::size_t>(*parsed);
                } else if (upperKey == "SYMBOLS") {
                    for (auto sym : splitFields(value, ',')) {
                        auto s = trim(sym);
                        if (!s.empty()) config.fmp->symbols.emplace_back(s);
                    }
                } else {
                    return err(lineNum, std::format(
                        "unknown key \"{}\" in [fmp] section", key));
                }
                continue;
            }

            // ── inside a screen section ──
            if (upperKey == "FILTER") {
                auto rule = parseFilterLine(value, lineNum, filepath);
                if (!rule) return std::unexpected(rule.error());
                current->filters.push_back(*rule);
            } else if (upperKey == "SORT") {
                if (!seenInScreen.insert(upperKey).second) {
                    return err(lineNum, std::format(
                        "duplicate \"sort\" in screen [{}]", current->name));
                }
                auto spec = parseSortLine(value, lineNum, filepath);
                if (!spec) return std::unexpected(spec.error());
                current->sort = *spec;
            } else if (upperKey == "OUTPUT") {
                if (!seenInScreen.insert(upperKey).second) {
                    return err(lineNum, std::format(
                        "duplicate \"output\" in screen [{}]", current->name));
                }
                current->outputFile = std::string{value};
            } else {
                return err(lineNum, std::format(
                    "unknown key \"{}\" in screen [{}] (expected filter/sort/output)",
                    key, current->name));
            }
        }

        if (current) config.screens.push_back(std::move(*current));

        if (config.screens.empty()) {
            return std::unexpected(std::format(
                "{}: no [screen:...] sections found", filepath));
        }

        if (config.source.empty()) {
            config.source = config.alpaca ? "alpaca" : ((config.fmp && config.fmp->enabled) ? "fmp" : "csv");
        }

        return config;
    }

} // namespace Config
