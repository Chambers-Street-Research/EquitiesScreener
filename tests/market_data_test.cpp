#include "io/MarketData.h"
#include <deque>
#include <gtest/gtest.h>
using namespace IO;
namespace
{
const std::string quote = R"({"symbol":"AAPL","quote":{"bp":100,"ap":102,"t":"2026-09-25T14:00:00Z"}})";
const std::string bars =
    R"({"symbol":"AAPL","bars":[{"t":"2026-09-24T04:00:00Z","o":99,"h":103,"l":98,"c":101,"v":500}],"next_page_token":null})";
struct Fake : HttpTransport
{
    std::deque<HttpResult> replies;
    std::vector<HttpRequest> requests;
    HttpResult get(const HttpRequest &r) override
    {
        requests.push_back(r);
        if (replies.empty())
            return std::unexpected(ApiError{ApiErrorKind::Connectivity, "unexpected request"});
        auto result = replies.front();
        replies.pop_front();
        return result;
    }
};
struct MarketTest : testing::Test
{
    Fake http;
    AlpacaOptions options{"key", "secret"};
    std::vector<long> delays;
    AlpacaClient client()
    {
        return AlpacaClient(options, http, [&](long ms) { delays.push_back(ms); });
    }
};
TEST_F(MarketTest, QuoteRetainsTimestampAndBidAsk)
{
    http.replies.push_back(HttpResponse{200, quote, {}});
    auto r = client().fetchQuote("AAPL");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->symbol, "AAPL");
    EXPECT_DOUBLE_EQ(r->bid, 100);
    EXPECT_DOUBLE_EQ(r->ask, 102);
    EXPECT_EQ(r->timestamp, "2026-09-25T14:00:00Z");
    EXPECT_EQ(http.requests[0].headers.at("APCA-API-KEY-ID"), "key");
    EXPECT_EQ(http.requests[0].headers.at("APCA-API-SECRET-KEY"), "secret");
    EXPECT_EQ(http.requests[0].parameters.at("feed"), "iex");
    EXPECT_GT(http.requests[0].timeoutMs, 0);
}
TEST_F(MarketTest, InvalidQuotes)
{
    for (auto body :
         {"broken", "{}", "{\"quote\":null}", "{\"symbol\":\"AAPL\",\"quote\":{\"bp\":0,\"ap\":2,\"t\":\"x\"}}",
          "{\"symbol\":\"AAPL\",\"quote\":{\"bp\":3,\"ap\":2,\"t\":\"x\"}}"})
    {
        http.replies.push_back(HttpResponse{200, body, {}});
        auto r = client().fetchQuote("AAPL");
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error().kind, ApiErrorKind::InvalidResponse);
    }
}
TEST_F(MarketTest, RateLimitHonorsRetryAfter)
{
    http.replies = {HttpResponse{429, "secret", "2"}, HttpResponse{200, quote, {}}};
    ASSERT_TRUE(client().fetchQuote("AAPL"));
    ASSERT_EQ(delays.size(), 1);
    EXPECT_EQ(delays[0], 2000);
}
TEST_F(MarketTest, LongRetryAfterDoesNotRetryEarly)
{
    http.replies = {HttpResponse{429, "", "3600"}};
    auto r = client().fetchQuote("AAPL");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().kind, ApiErrorKind::RateLimited);
    EXPECT_TRUE(delays.empty());
    EXPECT_EQ(http.requests.size(), 1);
}
TEST_F(MarketTest, BoundedRetriesAndRedactedErrors)
{
    http.replies = {HttpResponse{503, "secret", {}}, HttpResponse{503, "secret", {}}, HttpResponse{503, "secret", {}}};
    auto r = client().fetchQuote("AAPL");
    ASSERT_FALSE(r);
    EXPECT_EQ(http.requests.size(), 3);
    EXPECT_EQ(delays.size(), 2);
    EXPECT_EQ(r.error().message.find("secret"), std::string::npos);
}
TEST_F(MarketTest, PermanentErrorsAreNotRetried)
{
    for (int status : {400, 401, 403, 404})
    {
        auto before = http.requests.size();
        http.replies.push_back(HttpResponse{status, "secret", {}});
        EXPECT_FALSE(client().fetchQuote("AAPL"));
        EXPECT_EQ(http.requests.size(), before + 1);
    }
    EXPECT_TRUE(delays.empty());
}
TEST_F(MarketTest, RetryTimeoutAndConnectivity)
{
    for (auto kind : {ApiErrorKind::Timeout, ApiErrorKind::Connectivity})
    {
        http.replies = {std::unexpected(ApiError{kind, "network failure"}), HttpResponse{200, quote, {}}};
        EXPECT_TRUE(client().fetchQuote("AAPL"));
    }
    EXPECT_EQ(delays.size(), 2);
}
TEST_F(MarketTest, InputsValidatedBeforeNetwork)
{
    EXPECT_FALSE(client().fetchQuote("../bad?key=secret"));
    EXPECT_FALSE(client().fetchQuote(""));
    EXPECT_FALSE(client().fetchHistory("AAPL", "bad", "2026-09-25"));
    EXPECT_FALSE(client().fetchHistory("AAPL", "2026-09-26", "2026-09-25"));
    options.apiKey.clear();
    EXPECT_FALSE(client().fetchQuote("AAPL"));
    EXPECT_TRUE(http.requests.empty());
}
TEST_F(MarketTest, HistoryPagination)
{
    auto first = bars;
    first.replace(first.find("null"), 4, "\"next+page\"");
    http.replies = {HttpResponse{200, first, {}}, HttpResponse{200, bars, {}}};
    auto r = client().fetchHistory("AAPL", "2026-09-24", "2026-09-25");
    ASSERT_TRUE(r);
    ASSERT_EQ(r->size(), 2);
    EXPECT_DOUBLE_EQ((*r)[0].close, 101);
    EXPECT_DOUBLE_EQ((*r)[0].volume, 500);
    EXPECT_EQ(http.requests[1].parameters.at("page_token"), "next+page");
    EXPECT_EQ(http.requests[0].parameters.at("timeframe"), "1Day");
}
TEST_F(MarketTest, EmptyAndMalformedHistory)
{
    http.replies = {HttpResponse{200, R"({"bars":[],"next_page_token":null})", {}}};
    auto r = client().fetchHistory("AAPL", "2026-09-24", "2026-09-25");
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->empty());
    for (auto body : {"{}", "{\"bars\":[{}]}", "{\"bars\":[],\"next_page_token\":3}"})
    {
        http.replies.push_back(HttpResponse{200, body, {}});
        EXPECT_FALSE(client().fetchHistory("AAPL", "2026-09-24", "2026-09-25"));
    }
}
TEST_F(MarketTest, RepeatedPageTokenFails)
{
    auto body = bars;
    body.replace(body.find("null"), 4, "\"same\"");
    http.replies = {HttpResponse{200, body, {}}, HttpResponse{200, body, {}}};
    EXPECT_FALSE(client().fetchHistory("AAPL", "2026-09-24", "2026-09-25"));
    EXPECT_EQ(http.requests.size(), 2);
}
TEST_F(MarketTest, IngestionUsesMidpointWithMissingFundamentals)
{
    http.replies = {HttpResponse{200, quote, {}}};
    auto api = client();
    auto r = fetchQuoteUniverse(api, {"AAPL"});
    ASSERT_TRUE(r);
    ASSERT_EQ(r->size(), 1);
    EXPECT_FLOAT_EQ((*r)[0].getSpotPrice(), 101);
    EXPECT_FALSE((*r)[0].hasMetric(Data::Metric::PE));
}
TEST_F(MarketTest, IngestionFailsOnEmptyUniverseOrApiErrors)
{
    auto api = client();
    EXPECT_FALSE(fetchQuoteUniverse(api, {}));
    http.replies = {HttpResponse{401, "", {}}};
    EXPECT_FALSE(fetchQuoteUniverse(api, {"AAPL"}));
}
} // namespace
TEST(MarketErrors, ExhaustedTimeoutIsTypedAndBounded)
{
    Fake http;
    AlpacaOptions options{"key", "secret"};
    http.replies = {std::unexpected(ApiError{ApiErrorKind::Timeout, "timeout"}),
                    std::unexpected(ApiError{ApiErrorKind::Timeout, "timeout"}),
                    std::unexpected(ApiError{ApiErrorKind::Timeout, "timeout"})};
    AlpacaClient client(options, http, [](long) {});
    auto r = client.fetchQuote("AAPL");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().kind, ApiErrorKind::Timeout);
    EXPECT_EQ(http.requests.size(), 3);
}
TEST(MarketErrors, InsecureUrlsAndInvalidTimeoutsNeverSendCredentials)
{
    Fake http;
    AlpacaOptions options{"key", "secret"};
    for (auto url : {"http://example.com", "https://example.com?apikey=x", "http://127.0.0.1:80@evil.com"})
    {
        options.baseUrl = url;
        AlpacaClient client(options, http);
        EXPECT_FALSE(client.fetchQuote("AAPL"));
    }
    options.baseUrl = "https://data.alpaca.markets";
    for (long timeout : {0L, -1L, 60001L})
    {
        options.timeoutMs = timeout;
        AlpacaClient client(options, http);
        EXPECT_FALSE(client.fetchQuote("AAPL"));
    }
    EXPECT_TRUE(http.requests.empty());
}
TEST(MarketErrors, FutureRetryDateDoesNotRetryEarly)
{
    Fake http;
    http.replies = {HttpResponse{429, "", "Wed, 01 Jan 2031 00:00:00 GMT"}};
    AlpacaClient client({"key", "secret"}, http, [](long) { FAIL() << "must not sleep"; });
    EXPECT_FALSE(client.fetchQuote("AAPL"));
    EXPECT_EQ(http.requests.size(), 1);
}
TEST(MarketErrors, FailedSecondPageNeverReturnsPartialHistory)
{
    Fake http;
    auto first = bars;
    first.replace(first.find("null"), 4, "\"next\"");
    http.replies = {HttpResponse{200, first, {}}, HttpResponse{403, "", {}}};
    AlpacaClient client({"key", "secret"}, http);
    EXPECT_FALSE(client.fetchHistory("AAPL", "2026-09-24", "2026-09-25"));
}
TEST(MarketErrors, InvalidHistoricalPricesAndDates)
{
    Fake http;
    AlpacaClient client({"key", "secret"}, http);
    for (auto field : {"\"o\":99", "\"h\":103", "\"l\":98", "\"c\":101", "\"v\":500"})
    {
        auto body = bars;
        std::string target = field;
        body.replace(body.find(target), target.size(), target.substr(0, 4) + "-1");
        http.replies.push_back(HttpResponse{200, body, {}});
        EXPECT_FALSE(client.fetchHistory("AAPL", "2026-09-24", "2026-09-25"));
    }
    auto before = http.requests.size();
    EXPECT_FALSE(client.fetchHistory("AAPL", "2026-02-30", "2026-09-25"));
    EXPECT_EQ(http.requests.size(), before);
}
TEST(MarketErrors, DuplicateSymbolsOnlyFetchOnce)
{
    Fake http;
    http.replies = {HttpResponse{200, quote, {}}};
    AlpacaClient client({"key", "secret"}, http);
    auto r = fetchQuoteUniverse(client, {"AAPL", "AAPL"});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->size(), 1);
    EXPECT_EQ(http.requests.size(), 1);
}
