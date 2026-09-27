#include "MarketData.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cpr/cpr.h>
#include <ctime>
#include <curl/curl.h>
#include <limits>
#include <set>
#include <thread>

namespace IO
{
namespace
{
auto invalid()
{
    return std::unexpected(ApiError{ApiErrorKind::InvalidResponse, "Invalid market data response"});
}
bool dateValid(const std::string &s)
{
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        return false;
    int y = 0, m = 0, d = 0;
    auto parse = [&](int start, int size, int &out) {
        auto [p, e] = std::from_chars(s.data() + start, s.data() + start + size, out);
        return e == std::errc{} && p == s.data() + start + size;
    };
    return parse(0, 4, y) && parse(5, 2, m) && parse(8, 2, d) && y > 0 &&
           std::chrono::year_month_day{std::chrono::year{y}, std::chrono::month{unsigned(m)},
                                       std::chrono::day{unsigned(d)}}
               .ok();
}
bool positive(const std::optional<double> &n)
{
    return n && std::isfinite(*n) && *n > 0;
}
// Never shorten a server-directed wait: long waits return an error to the caller.
long retryDelay(const std::string &value, long fallback)
{
    if (value.empty())
        return fallback;
    long seconds = 0;
    auto [p, e] = std::from_chars(value.data(), value.data() + value.size(), seconds);
    if (e == std::errc::result_out_of_range)
        return 5001;
    if (e == std::errc{} && p == value.data() + value.size() && seconds >= 0)
        return seconds > 5 ? 5001 : seconds * 1000;
    auto when = curl_getdate(value.c_str(), nullptr);
    if (when < 0)
        return fallback;
    auto delta = when - std::time(nullptr);
    return delta > 5 ? 5001 : std::max<long>(0, delta) * 1000;
}
} // namespace
HttpResult CprTransport::get(const HttpRequest &request)
{
    if (request.timeoutMs < 1 || request.timeoutMs > 60000)
        return std::unexpected(ApiError{ApiErrorKind::Configuration, "Invalid HTTP timeout"});
    cpr::Header headers;
    cpr::Parameters parameters;
    for (const auto &[key, value] : request.headers)
        headers[key] = value;
    for (const auto &[key, value] : request.parameters)
        parameters.Add({key, value});
    auto response =
        cpr::Get(cpr::Url{request.url}, headers, parameters, cpr::Timeout{static_cast<int>(request.timeoutMs)},
                 cpr::ConnectTimeout{static_cast<int>(std::min(2000L, request.timeoutMs))}, cpr::Redirect{false});
    if (response.error.code != cpr::ErrorCode::OK)
    {
        const bool timeout = response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT;
        return std::unexpected(ApiError{timeout ? ApiErrorKind::Timeout : ApiErrorKind::Connectivity,
                                        timeout ? "Market data request timeout" : "Market data connection failed"});
    }
    auto retry = response.header["Retry-After"];
    // Alpaca also reports its rate-limit reset as a Unix timestamp.
    if (retry.empty() && response.status_code == 429)
    {
        auto reset = response.header["X-RateLimit-Reset"];
        long seconds = 0;
        auto [p, e] = std::from_chars(reset.data(), reset.data() + reset.size(), seconds);
        if (e == std::errc{} && p == reset.data() + reset.size())
            retry = std::to_string(std::max<long>(0, seconds - std::time(nullptr)));
    }
    return HttpResponse{response.status_code, std::move(response.text), std::move(retry)};
}
AlpacaClient::AlpacaClient(AlpacaOptions options, HttpTransport &transport, std::function<void(long)> sleep)
    : options_(std::move(options)), transport_(transport), sleep_(std::move(sleep))
{
    while (options_.baseUrl.ends_with('/'))
        options_.baseUrl.pop_back();
    if (!sleep_)
        sleep_ = [](long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
}
ApiResult<void> AlpacaClient::validate(const std::string &symbol) const
{
    if (options_.apiKey.empty() || options_.secretKey.empty())
        return std::unexpected(ApiError{ApiErrorKind::Configuration,
                                        "Set APCA_API_KEY_ID and APCA_API_SECRET_KEY or [alpaca] credentials"});
    if (options_.apiKey.find_first_of("\r\n") != std::string::npos ||
        options_.secretKey.find_first_of("\r\n") != std::string::npos)
        return std::unexpected(ApiError{ApiErrorKind::Configuration, "Invalid market data credentials"});
    bool secure = options_.baseUrl.starts_with("https://") && options_.baseUrl.size() > 8;
    bool local = options_.baseUrl.starts_with("http://127.0.0.1:");
    if ((!secure && !local) || options_.baseUrl.find_first_of("?#@\r\n") != std::string::npos ||
        options_.timeoutMs < 1 || options_.timeoutMs > 60000 || (options_.feed != "iex" && options_.feed != "sip"))
        return std::unexpected(
            ApiError{ApiErrorKind::Configuration, "Invalid Alpaca URL, feed, or timeout (1..60000 ms)"});
    if (symbol.empty() || symbol.size() > 32 || !std::all_of(symbol.begin(), symbol.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        }))
        return std::unexpected(ApiError{ApiErrorKind::Configuration, "Invalid stock symbol (use uppercase ticker)"});
    return {};
}
ApiResult<JsonValue> AlpacaClient::request(const std::string &path, std::map<std::string, std::string> parameters)
{
    parameters["feed"] = options_.feed;
    HttpRequest req{options_.baseUrl + path,
                    {{"APCA-API-KEY-ID", options_.apiKey}, {"APCA-API-SECRET-KEY", options_.secretKey}},
                    std::move(parameters),
                    options_.timeoutMs};
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        auto response = transport_.get(req);
        ApiError error{ApiErrorKind::Http, "Market data HTTP error"};
        bool retry = false;
        long delay = 250L << attempt;
        if (!response)
        {
            error = response.error();
            retry = error.kind == ApiErrorKind::Timeout || error.kind == ApiErrorKind::Connectivity;
        }
        else if (response->status == 200)
        {
            auto json = parseJson(response->body);
            if (!json)
                return invalid();
            return std::move(*json);
        }
        else
        {
            long status = response->status;
            error.message += " (" + std::to_string(status) + ")";
            if (status == 401 || status == 403)
                error.kind = ApiErrorKind::Authentication;
            if (status == 429)
                error.kind = ApiErrorKind::RateLimited;
            retry = status == 429 || status == 500 || status == 502 || status == 503 || status == 504;
            delay = retryDelay(response->retryAfter, delay);
        }
        if (!retry || attempt == 2 || delay > 5000)
            return std::unexpected(std::move(error));
        sleep_(delay);
    }
    return invalid();
}
ApiResult<Quote> AlpacaClient::fetchQuote(const std::string &symbol)
{
    if (auto valid = validate(symbol); !valid)
        return std::unexpected(valid.error());
    auto json = request("/v2/stocks/" + symbol + "/quotes/latest", {});
    if (!json)
        return std::unexpected(json.error());
    const auto &q = (*json)["quote"];
    auto bid = q["bp"].asDouble(), ask = q["ap"].asDouble();
    auto timestamp = q["t"].asString();
    if ((*json)["symbol"].asString() != symbol || !positive(bid) || !positive(ask) || *ask < *bid || !timestamp ||
        timestamp->empty())
        return invalid();
    return Quote{symbol, std::string(*timestamp), *bid, *ask};
}
ApiResult<std::vector<PriceBar>> AlpacaClient::fetchHistory(const std::string &symbol, const std::string &start,
                                                            const std::string &end)
{
    if (auto valid = validate(symbol); !valid)
        return std::unexpected(valid.error());
    if (!dateValid(start) || !dateValid(end) || start > end)
        return std::unexpected(ApiError{ApiErrorKind::Configuration, "History requires ordered YYYY-MM-DD dates"});
    std::map<std::string, std::string> params{{"start", start},      {"end", end},      {"timeframe", "1Day"},
                                              {"adjustment", "raw"}, {"limit", "1000"}, {"sort", "asc"}};
    std::vector<PriceBar> result;
    std::set<std::string> tokens;
    for (int page = 0; page < 100; ++page)
    {
        auto json = request("/v2/stocks/" + symbol + "/bars", params);
        if (!json)
            return std::unexpected(json.error());
        auto arr = (*json)["bars"].asArray();
        if (!arr || (json->contains("symbol") && (*json)["symbol"].asString() != symbol))
            return invalid();
        for (const auto &b : *arr)
        {
            auto t = b["t"].asString();
            auto o = b["o"].asDouble(), h = b["h"].asDouble(), l = b["l"].asDouble(), c = b["c"].asDouble(),
                 v = b["v"].asDouble();
            if (!t || t->empty() || !positive(o) || !positive(h) || !positive(l) || !positive(c) || !v ||
                !std::isfinite(*v) || *v < 0 || *l > std::min(*o, *c) || *h < std::max(*o, *c))
                return invalid();
            result.push_back({std::string(*t), *o, *h, *l, *c, *v});
            if (result.size() > 100000)
                return invalid();
        }
        const auto &next = (*json)["next_page_token"];
        if (next.isNull())
            return result;
        auto token = next.asString();
        if (!token)
            return invalid();
        if (token->empty())
            return result;
        if (!tokens.insert(std::string(*token)).second)
            return invalid();
        params["page_token"] = *token;
    }
    return std::unexpected(ApiError{ApiErrorKind::InvalidResponse, "Historical pagination exceeded 100 pages"});
}
ApiResult<std::vector<Data::Equity>> fetchQuoteUniverse(MarketDataClient &client,
                                                        const std::vector<std::string> &symbols)
{
    if (symbols.empty())
        return std::unexpected(ApiError{ApiErrorKind::Configuration, "Configure [alpaca] symbols"});
    std::vector<Data::Equity> result;
    std::set<std::string> seen;
    for (const auto &symbol : symbols)
    {
        if (!seen.insert(symbol).second)
            continue;
        auto quote = client.fetchQuote(symbol);
        if (!quote)
            return std::unexpected(quote.error());
        double midpoint = quote->bid / 2 + quote->ask / 2;
        if (!std::isfinite(midpoint) || midpoint <= 0 || midpoint > std::numeric_limits<float>::max())
            return invalid();
        result.emplace_back(symbol, "", symbol, static_cast<float>(midpoint));
    }
    return result;
}
} // namespace IO
