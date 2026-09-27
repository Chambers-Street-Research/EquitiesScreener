#include "FmpClient.h"

#include <curl/curl.h>

#include <cstdlib>
#include <format>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace IO {

    namespace {

        size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            size_t total = size * nmemb;
            auto* out = static_cast<std::string*>(userp);
            out->append(static_cast<char*>(contents), total);
            return total;
        }

        std::string urlEncode(std::string_view value) {
            std::ostringstream escaped;
            escaped.fill('0');
            escaped << std::hex;

            for (char c : value) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
                    escaped << c;
                } else {
                    escaped << '%' << std::uppercase
                            << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
                }
            }
            return escaped.str();
        }

    } // anonymous namespace

    FmpClient::FmpClient(std::string apiKey,
                         std::string baseUrl,
                         long timeoutSec,
                         ProgressCallback progressCallback)
        : apiKey_(std::move(apiKey)),
          baseUrl_(std::move(baseUrl)),
          timeoutSec_(timeoutSec),
          progressCallback_(std::move(progressCallback)) {
        if (apiKey_.empty()) {
            if (const char* env = std::getenv("FMP_API_KEY")) {
                apiKey_ = env;
            }
        }
    }

    std::expected<std::string, std::string> FmpClient::httpGet(std::string_view url) const {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return std::unexpected("Failed to initialize libcurl");
        }

        std::string responseBody;
        std::string urlStr(url);

        curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec_);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "EquitiesScreener/1.0");

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::string err = std::format("CURL error: {}", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            return std::unexpected(err);
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        if (httpCode == 401 || httpCode == 403) {
            return std::unexpected(std::format(
                "FMP API authorization failed (HTTP {}): Invalid or missing API key.", httpCode));
        }
        if (httpCode == 429) {
            return std::unexpected("FMP API rate limit reached (HTTP 429: daily limit exceeded).");
        }
        if (httpCode != 200) {
            return std::unexpected(std::format("FMP API returned unexpected HTTP status {}", httpCode));
        }

        return responseBody;
    }

    std::expected<std::vector<Data::Equity>, std::string>
    FmpClient::fetchBySector(std::string_view sector, std::size_t limit) const {
        if (apiKey_.empty()) {
            return std::unexpected("FMP API key is not configured. Set FMP_API_KEY environment variable or [fmp] api_key.");
        }

        std::string url;
        if (sector.empty()) {
            url = std::format("{}/stock-screener?isActivelyTrading=true&limit={}&apikey={}",
                              baseUrl_, limit, apiKey_);
        } else {
            url = std::format("{}/stock-screener?sector={}&isActivelyTrading=true&limit={}&apikey={}",
                              baseUrl_, urlEncode(sector), limit, apiKey_);
        }

        auto body = httpGet(url);
        if (!body) return std::unexpected(body.error());

        auto json = parseJson(*body);
        if (!json) return std::unexpected(json.error());

        if (const auto* obj = json->asObject()) {
            if (auto err = obj->find("Error Message"); err != obj->end()) {
                if (auto errMsg = err->second.asString()) {
                    return std::unexpected(std::format("FMP API error: {}", *errMsg));
                }
            }
        }

        const auto* arr = json->asArray();
        if (!arr) {
            return std::unexpected("Unexpected response from FMP stock-screener: expected JSON array");
        }

        std::vector<Data::Equity> equities;
        equities.reserve(arr->size());

        for (const auto& item : *arr) {
            auto sym = item["symbol"].asString().value_or("");
            auto name = item["companyName"].asString().value_or(std::string(sym));
            auto exchange = item["exchangeShortName"].asString().value_or(
                item["exchange"].asString().value_or(""));
            auto sec = item["sector"].asString().value_or("");
            auto price = item["price"].asFloat().value_or(0.0f);

            if (sym.empty() || price <= 0.0f) continue;

            Data::Equity eq(std::string(name), std::string(exchange), std::string(sym), price);
            eq.setSector(std::string(sec));
            equities.push_back(std::move(eq));
        }

        return equities;
    }

    std::expected<std::vector<Data::Equity>, std::string>
    FmpClient::fetchProfiles(std::span<const std::string> symbols) const {
        if (apiKey_.empty()) {
            return std::unexpected("FMP API key is not configured. Set FMP_API_KEY environment variable or [fmp] api_key.");
        }
        if (symbols.empty()) {
            return std::vector<Data::Equity>{};
        }

        std::vector<Data::Equity> equities;
        equities.reserve(symbols.size());

        // Batch in chunks of 50 symbols
        constexpr std::size_t batchSize = 50;
        for (std::size_t i = 0; i < symbols.size(); i += batchSize) {
            std::size_t end = std::min(i + batchSize, symbols.size());
            std::string symBatch;
            for (std::size_t j = i; j < end; ++j) {
                if (!symBatch.empty()) symBatch += ",";
                symBatch += symbols[j];
            }

            std::string url = std::format("{}/profile/{}?apikey={}", baseUrl_, symBatch, apiKey_);
            auto body = httpGet(url);
            if (!body) return std::unexpected(body.error());

            auto json = parseJson(*body);
            if (!json) return std::unexpected(json.error());

            const auto* arr = json->asArray();
            if (!arr) continue;

            for (const auto& item : *arr) {
                auto sym = item["symbol"].asString().value_or("");
                auto name = item["companyName"].asString().value_or(std::string(sym));
                auto exchange = item["exchangeShortName"].asString().value_or(
                    item["exchange"].asString().value_or(""));
                auto sec = item["sector"].asString().value_or("");
                auto price = item["price"].asFloat().value_or(0.0f);

                if (sym.empty() || price <= 0.0f) continue;

                Data::Equity eq(std::string(name), std::string(exchange), std::string(sym), price);
                eq.setSector(std::string(sec));
                equities.push_back(std::move(eq));
            }
        }

        return equities;
    }

    std::expected<void, std::string> FmpClient::enrichFundamentals(Data::Equity& equity) const {
        if (apiKey_.empty()) {
            return std::unexpected("FMP API key is not configured.");
        }

        const auto& sym = equity.getTicker();

        // 1. Fetch ratios-ttm
        std::string ratiosUrl = std::format("{}/ratios-ttm/{}?apikey={}", baseUrl_, sym, apiKey_);
        auto ratiosBody = httpGet(ratiosUrl);
        if (ratiosBody) {
            if (auto json = parseJson(*ratiosBody)) {
                if (const auto* arr = json->asArray(); arr && !arr->empty()) {
                    const auto& r = (*arr)[0];
                    if (auto v = r["peRatioTTM"].asFloat(); v && *v > 0.0f) {
                        equity.setPERatio(*v);
                    } else if (auto v2 = r["priceEarningsRatioTTM"].asFloat(); v2 && *v2 > 0.0f) {
                        equity.setPERatio(*v2);
                    }
                    if (auto v = r["priceToBookRatioTTM"].asFloat()) equity.setPBRatio(*v);
                    if (auto v = r["priceToSalesRatioTTM"].asFloat()) equity.setPSRatio(*v);
                    if (auto v = r["enterpriseValueMultipleTTM"].asFloat()) equity.setEVEBITDA(*v);
                    if (auto v = r["returnOnAssetsTTM"].asFloat()) equity.setROA(*v);
                    if (auto v = r["returnOnEquityTTM"].asFloat()) equity.setROE(*v);
                    if (auto v = r["returnOnCapitalEmployedTTM"].asFloat()) equity.setROIC(*v);
                }
            }
        }

        // 2. Fetch key-metrics-ttm for additional coverage (e.g. roicTTM, enterpriseValueOverEBITDATTM)
        std::string metricsUrl = std::format("{}/key-metrics-ttm/{}?apikey={}", baseUrl_, sym, apiKey_);
        auto metricsBody = httpGet(metricsUrl);
        if (metricsBody) {
            if (auto json = parseJson(*metricsBody)) {
                if (const auto* arr = json->asArray(); arr && !arr->empty()) {
                    const auto& m = (*arr)[0];
                    if (!equity.hasMetric(Data::Metric::EVEBITDA)) {
                        if (auto v = m["enterpriseValueOverEBITDATTM"].asFloat()) equity.setEVEBITDA(*v);
                    }
                    if (!equity.hasMetric(Data::Metric::ROIC)) {
                        if (auto v = m["roicTTM"].asFloat()) equity.setROIC(*v);
                    }
                    if (!equity.hasMetric(Data::Metric::ROE)) {
                        if (auto v = m["roeTTM"].asFloat()) equity.setROE(*v);
                    }
                    if (!equity.hasMetric(Data::Metric::ROA)) {
                        if (auto v = m["returnOnTangibleAssetsTTM"].asFloat()) equity.setROA(*v);
                    }
                    if (!equity.hasMetric(Data::Metric::PB)) {
                        if (auto v = m["pbRatioTTM"].asFloat()) equity.setPBRatio(*v);
                    }
                    if (!equity.hasMetric(Data::Metric::PS)) {
                        if (auto v = m["priceToSalesRatioTTM"].asFloat()) equity.setPSRatio(*v);
                    }
                }
            }
        }

        return {};
    }

    std::expected<std::vector<Data::Equity>, std::string>
    FmpClient::fetchEquities(const FmpFetchOptions& options) const {
        std::string effectiveKey = options.apiKey.empty() ? apiKey_ : options.apiKey;
        if (effectiveKey.empty()) {
            if (const char* env = std::getenv("FMP_API_KEY")) {
                effectiveKey = env;
            }
        }
        if (effectiveKey.empty()) {
            return std::unexpected(
                "FMP API key not provided. Set FMP_API_KEY environment variable or [fmp] api_key in settings file.");
        }

        FmpClient client(effectiveKey, options.baseUrl, options.timeoutSec, progressCallback_);

        std::vector<Data::Equity> baseList;
        if (!options.symbols.empty()) {
            if (progressCallback_) {
                progressCallback_(std::format("Fetching profiles for {} symbols from FMP...", options.symbols.size()));
            }
            auto res = client.fetchProfiles(options.symbols);
            if (!res) return std::unexpected(res.error());
            baseList = std::move(*res);
        } else if (!options.sector.empty()) {
            if (progressCallback_) {
                progressCallback_(std::format("Fetching top {} stocks in sector '{}' from FMP...",
                                              options.limit, options.sector));
            }
            auto res = client.fetchBySector(options.sector, options.limit);
            if (!res) return std::unexpected(res.error());
            baseList = std::move(*res);
        } else {
            if (progressCallback_) {
                progressCallback_(std::format("Fetching top {} stocks from FMP...", options.limit));
            }
            auto res = client.fetchBySector("", options.limit);
            if (!res) return std::unexpected(res.error());
            baseList = std::move(*res);
        }

        if (baseList.empty()) {
            return std::unexpected("No stocks found matching the specified criteria from FMP.");
        }

        // Enrich each with fundamentals
        std::size_t idx = 0;
        for (auto& eq : baseList) {
            ++idx;
            if (progressCallback_) {
                progressCallback_(std::format("Retrieving fundamentals for {} ({}/{})",
                                              eq.getTicker(), idx, baseList.size()));
            }
            auto enrichRes = client.enrichFundamentals(eq);
            if (!enrichRes && progressCallback_) {
                progressCallback_(std::format("Warning: could not fetch fundamentals for {}: {}",
                                              eq.getTicker(), enrichRes.error()));
            }
        }

        return baseList;
    }

} // namespace IO
