#include "io/Json.h"

#include <gtest/gtest.h>

TEST(JsonParserTest, Primitives) {
    auto nullVal = IO::parseJson("null");
    ASSERT_TRUE(nullVal.has_value());
    EXPECT_TRUE(nullVal->isNull());

    auto trueVal = IO::parseJson("true");
    ASSERT_TRUE(trueVal.has_value());
    EXPECT_EQ(trueVal->asBool(), true);

    auto falseVal = IO::parseJson("false");
    ASSERT_TRUE(falseVal.has_value());
    EXPECT_EQ(falseVal->asBool(), false);

    auto intVal = IO::parseJson("42");
    ASSERT_TRUE(intVal.has_value());
    EXPECT_EQ(intVal->asInt(), 42);

    auto floatVal = IO::parseJson("123.45");
    ASSERT_TRUE(floatVal.has_value());
    EXPECT_NEAR(*floatVal->asFloat(), 123.45f, 0.001f);

    auto strVal = IO::parseJson("\"hello world\"");
    ASSERT_TRUE(strVal.has_value());
    EXPECT_EQ(strVal->asString(), "hello world");
}

TEST(JsonParserTest, Array) {
    auto arrVal = IO::parseJson("[1, 2, \"test\", false]");
    ASSERT_TRUE(arrVal.has_value());
    EXPECT_TRUE(arrVal->isArray());
    EXPECT_EQ(arrVal->size(), 4u);
    EXPECT_EQ((*arrVal)[0].asInt(), 1);
    EXPECT_EQ((*arrVal)[1].asInt(), 2);
    EXPECT_EQ((*arrVal)[2].asString(), "test");
    EXPECT_EQ((*arrVal)[3].asBool(), false);
}

TEST(JsonParserTest, Object) {
    auto objVal = IO::parseJson(R"({
        "symbol": "AAPL",
        "price": 220.5,
        "active": true,
        "metrics": {
            "pe": 33.5,
            "roe": 1.5
        }
    })");
    ASSERT_TRUE(objVal.has_value());
    EXPECT_TRUE(objVal->isObject());
    EXPECT_EQ((*objVal)["symbol"].asString(), "AAPL");
    EXPECT_NEAR(*(*objVal)["price"].asFloat(), 220.5f, 0.01f);
    EXPECT_EQ((*objVal)["active"].asBool(), true);

    const auto& metrics = (*objVal)["metrics"];
    EXPECT_TRUE(metrics.isObject());
    EXPECT_NEAR(*metrics["pe"].asFloat(), 33.5f, 0.01f);
    EXPECT_NEAR(*metrics["roe"].asFloat(), 1.5f, 0.01f);
}

TEST(JsonParserTest, Malformed) {
    auto bad1 = IO::parseJson("{ invalid }");
    EXPECT_FALSE(bad1.has_value());

    auto bad2 = IO::parseJson("[1, 2,");
    EXPECT_FALSE(bad2.has_value());

    auto bad3 = IO::parseJson("\"unterminated");
    EXPECT_FALSE(bad3.has_value());
}

