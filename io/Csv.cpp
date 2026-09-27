#include "Csv.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <fstream>
#include <ranges>
#include <system_error>

namespace IO {

    // ────────────────────────────────────────────────────
    //  Internal helpers
    // ────────────────────────────────────────────────────

    namespace {

        /// Trim leading and trailing whitespace from a string_view (in-place).
        constexpr auto trim(std::string_view sv) noexcept -> std::string_view {
            auto start = sv.begin();
            auto end   = sv.end();
            while (start != end && std::isspace(static_cast<unsigned char>(*start))) ++start;
            while (start != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
            return {start, end};
        }

        /// Parse a float from a string_view. Returns nullopt on failure.
        auto parseFloat(std::string_view sv) noexcept -> std::optional<float> {
            sv = trim(sv);
            float val{};
            auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
            if (ec != std::errc{}) return std::nullopt;
            return val;
        }

        /// Split a line by commas into string_views (zero-copy).
        auto splitFields(std::string_view line)
            -> std::vector<std::string_view> {
            std::vector<std::string_view> fields;

            std::size_t start = 0;
            while (start <= line.size()) {
                const std::size_t comma = line.find(',', start);

                if (comma == std::string_view::npos) {
                    fields.emplace_back(line.substr(start));
                    break;
                }

                fields.emplace_back(line.substr(start, comma - start));
                start = comma + 1;
            }

            return fields;
        }

        /// CSV column indices (zero-based).
        enum Column : size_t {
            ColTicker = 0,
            ColName,
            ColExchange,
            ColSector,
            ColSpotPrice,
            ColPE,
            ColForwardPE,
            ColPB,
            ColPS,
            ColEVEBITDA,
            ColROA,
            ColROE,
            ColROIC,
            ColCount       // must be last — total expected columns
        };

    } // anonymous namespace

    // ────────────────────────────────────────────────────
    //  Public API implementations
    // ────────────────────────────────────────────────────

    std::expected<Data::Equity, std::string> parseEquity(std::string_view line) {
        auto fields = splitFields(line);

        if (fields.size() < ColCount) {
            return std::unexpected(
                std::format("expected {} fields, got {}", static_cast<size_t>(ColCount), fields.size()));
        }

        // ── spot_price (required for constructor) ──
        auto spot = parseFloat(fields[ColSpotPrice]);
        if (!spot) {
            return std::unexpected(
                std::format("invalid SpotPrice: \"{}\"", trim(fields[ColSpotPrice])));
        }

        // ── build the Equity ──
        Data::Equity eq(
            std::string{trim(fields[ColName])},
            std::string{trim(fields[ColExchange])},
            std::string{trim(fields[ColTicker])},
            *spot
        );

        eq.setSector(std::string{trim(fields[ColSector])});

        // ── optional numeric fields (parseFloat returns nullopt → skip silently) ──
        auto setIf = [&](Column col, void (Data::Equity::*setter)(float)) {
            if (auto v = parseFloat(fields[col])) { (eq.*setter)(*v); }
        };

        setIf(ColPE,        &Data::Equity::setPERatio);
        setIf(ColForwardPE, &Data::Equity::setForwardPE);
        setIf(ColPB,        &Data::Equity::setPBRatio);
        setIf(ColPS,        &Data::Equity::setPSRatio);
        setIf(ColEVEBITDA,  &Data::Equity::setEVEBITDA);
        setIf(ColROA,       &Data::Equity::setROA);
        setIf(ColROE,       &Data::Equity::setROE);
        setIf(ColROIC,      &Data::Equity::setROIC);

        return eq;
    }

    std::expected<CsvReadResult, CsvError> readCsv(std::string_view filepath) {
        std::ifstream file{std::string{filepath}};
        if (!file.is_open())   return std::unexpected(CsvError::FileNotFound);

        std::string line;
        if (!std::getline(file, line)) return std::unexpected(CsvError::EmptyFile);
        // first line is header — consumed above, not parsed

        CsvReadResult result;
        size_t lineNum = 1;  // header was line 1
        while (std::getline(file, line)) {
            ++lineNum;
            auto trimmed = trim(line);
            if (trimmed.empty()) continue;          // skip blank lines

            auto parsed = parseEquity(trimmed);
            if (parsed) {
                result.equities.push_back(std::move(*parsed));
            } else {
                result.warnings.push_back(
                    std::format("Line {}: {}", lineNum, parsed.error()));
            }
        }

        return result;
    }

    std::expected<void, CsvError> writeCsv(std::string_view filepath,
                                           std::span<const Data::Equity> equities) {
        std::ofstream file{std::string{filepath}};
        if (!file.is_open()) return std::unexpected(CsvError::WriteError);
        return writeCsv(file, equities);
    }

    std::expected<void, CsvError> writeCsv(std::ostream& os,
                                           std::span<const Data::Equity> equities) {
        os << "ticker,name,exchange,sector,spot_price,"
              "pe,forward_pe,pb,ps,ev_ebitda,roa,roe,roic\n";

        for (const auto& eq : equities) {
            os << std::format(
                "{},{},{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.4f},{:.4f},{:.4f}\n",
                eq.getTicker(),
                eq.getName(),
                eq.getExchange(),
                eq.getSector(),
                eq.getSpotPrice(),
                eq.getPERatio(),
                eq.getForwardPE(),
                eq.getPBRatio(),
                eq.getPSRatio(),
                eq.getEVEBITDA(),
                eq.getROA(),
                eq.getROE(),
                eq.getROIC()
            );
            if (os.fail()) return std::unexpected(CsvError::WriteError);
        }

        return {};
    }

} // namespace IO
