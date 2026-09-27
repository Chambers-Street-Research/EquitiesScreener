#ifndef EQUITIESSCREENER_IO_FMPCLIENT_H
#define EQUITIESSCREENER_IO_FMPCLIENT_H

#include "../data/Equity.h"
#include "Json.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IO {

    struct FmpFetchOptions {
        std::string              apiKey;
        std::string              baseUrl{"https://financialmodelingprep.com/api/v3"};
        std::vector<std::string> symbols;
        std::string              sector;
        std::size_t              limit{25};
        long                     timeoutSec{15};
    };

    class FmpClient {
    public:
        using ProgressCallback = std::function<void(std::string_view)>;

    private:
        std::string apiKey_;
        std::string baseUrl_;
        long timeoutSec_{15};
        ProgressCallback progressCallback_;

        [[nodiscard]] std::expected<std::string, std::string> httpGet(std::string_view url) const;

    public:
        explicit FmpClient(std::string apiKey = {},
                           std::string baseUrl = "https://financialmodelingprep.com/api/v3",
                           long timeoutSec = 15,
                           ProgressCallback progressCallback = {});

        void setProgressCallback(ProgressCallback cb) { progressCallback_ = std::move(cb); }

        /// Fetch stocks matching a sector via FMP's stock-screener endpoint.
        [[nodiscard]] std::expected<std::vector<Data::Equity>, std::string>
        fetchBySector(std::string_view sector, std::size_t limit) const;

        /// Batch fetch company profiles for a list of symbols.
        [[nodiscard]] std::expected<std::vector<Data::Equity>, std::string>
        fetchProfiles(std::span<const std::string> symbols) const;

        /// Enrich a single Equity with TTM fundamental ratios (PE, PB, PS, EV/EBITDA, ROA, ROE, ROIC).
        [[nodiscard]] std::expected<void, std::string>
        enrichFundamentals(Data::Equity& equity) const;

        /// High-level method: resolve symbols/sector, fetch profile & price data,
        /// and enrich with fundamental ratios.
        [[nodiscard]] std::expected<std::vector<Data::Equity>, std::string>
        fetchEquities(const FmpFetchOptions& options) const;
    };

} // namespace IO

#endif // EQUITIESSCREENER_IO_FMPCLIENT_H

