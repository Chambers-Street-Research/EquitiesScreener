#pragma once
#include "../data/Equity.h"
#include "Json.h"
#include <expected>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace IO
{
enum class ApiErrorKind
{
    Configuration,
    Authentication,
    RateLimited,
    Timeout,
    Connectivity,
    Http,
    InvalidResponse
};
struct ApiError
{
    ApiErrorKind kind;
    std::string message;
};
template <class T> using ApiResult = std::expected<T, ApiError>;
struct HttpRequest
{
    std::string url;
    std::map<std::string, std::string> headers, parameters;
    long timeoutMs{5000};
};
struct HttpResponse
{
    long status;
    std::string body, retryAfter;
};
using HttpResult = ApiResult<HttpResponse>;
class HttpTransport
{
  public:
    virtual ~HttpTransport() = default;
    virtual HttpResult get(const HttpRequest &) = 0;
};
class CprTransport final : public HttpTransport
{
  public:
    HttpResult get(const HttpRequest &) override;
};
struct Quote
{
    std::string symbol, timestamp;
    double bid, ask;
};
struct PriceBar
{
    std::string timestamp;
    double open, high, low, close, volume;
};
// Provider-neutral boundary for a future ETL/data-server adapter.
class MarketDataClient
{
  public:
    virtual ~MarketDataClient() = default;
    virtual ApiResult<Quote> fetchQuote(const std::string &symbol) = 0;
    // Inclusive dates in YYYY-MM-DD; daily, unadjusted OHLCV bars.
    virtual ApiResult<std::vector<PriceBar>> fetchHistory(const std::string &symbol, const std::string &start,
                                                          const std::string &end) = 0;
};
struct AlpacaOptions
{
    std::string apiKey, secretKey;
    std::string baseUrl{"https://data.alpaca.markets"};
    std::string feed{"iex"};
    long timeoutMs{5000};
};
class AlpacaClient final : public MarketDataClient
{
    AlpacaOptions options_;
    HttpTransport &transport_;
    std::function<void(long)> sleep_;
    ApiResult<void> validate(const std::string &symbol) const;
    ApiResult<JsonValue> request(const std::string &path, std::map<std::string, std::string> parameters);

  public:
    // Dependencies must outlive the client. Empty sleep uses std::this_thread::sleep_for.
    AlpacaClient(AlpacaOptions options, HttpTransport &transport, std::function<void(long)> sleep = {});
    ApiResult<Quote> fetchQuote(const std::string &symbol) override;
    ApiResult<std::vector<PriceBar>> fetchHistory(const std::string &symbol, const std::string &start,
                                                  const std::string &end) override;
};
ApiResult<std::vector<Data::Equity>> fetchQuoteUniverse(MarketDataClient &client,
                                                        const std::vector<std::string> &symbols);
} // namespace IO
