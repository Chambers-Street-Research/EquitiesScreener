#include "Json.h"

#include <cctype>
#include <charconv>
#include <format>
#include <string>
#include <string_view>

namespace IO {

    namespace {

        class JsonParser {
        private:
            std::string_view text_;
            std::size_t pos_{0};

            void skipWhitespace() noexcept {
                while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
                    ++pos_;
                }
            }

            [[nodiscard]] bool atEnd() const noexcept {
                return pos_ >= text_.size();
            }

            [[nodiscard]] char peek() const noexcept {
                if (atEnd()) return '\0';
                return text_[pos_];
            }

            char get() noexcept {
                if (atEnd()) return '\0';
                return text_[pos_++];
            }

            [[nodiscard]] std::string error(std::string_view msg) const {
                return std::format("JSON parse error at character {}: {}", pos_, msg);
            }

            std::expected<std::string, std::string> parseString() {
                if (get() != '"') return std::unexpected(error("expected '\"'"));
                std::string result;
                while (!atEnd()) {
                    char c = get();
                    if (c == '"') {
                        return result;
                    }
                    if (c == '\\') {
                        if (atEnd()) return std::unexpected(error("unexpected EOF inside escape sequence"));
                        char esc = get();
                        switch (esc) {
                            case '"':  result.push_back('"'); break;
                            case '\\': result.push_back('\\'); break;
                            case '/':  result.push_back('/'); break;
                            case 'b':  result.push_back('\b'); break;
                            case 'f':  result.push_back('\f'); break;
                            case 'n':  result.push_back('\n'); break;
                            case 'r':  result.push_back('\r'); break;
                            case 't':  result.push_back('\t'); break;
                            case 'u': {
                                // 4 hex digits
                                if (pos_ + 4 > text_.size()) return std::unexpected(error("malformed \\u escape"));
                                std::string_view hex = text_.substr(pos_, 4);
                                pos_ += 4;
                                unsigned int codepoint = 0;
                                auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), codepoint, 16);
                                if (ec != std::errc{}) return std::unexpected(error("invalid hex in \\u escape"));
                                if (codepoint <= 0x7F) {
                                    result.push_back(static_cast<char>(codepoint));
                                } else if (codepoint <= 0x7FF) {
                                    result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                                } else {
                                    result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                                }
                                break;
                            }
                            default:
                                result.push_back(esc);
                                break;
                        }
                    } else {
                        result.push_back(c);
                    }
                }
                return std::unexpected(error("unterminated string"));
            }

            std::expected<double, std::string> parseNumber() {
                std::size_t start = pos_;
                if (peek() == '-') get();
                while (std::isdigit(static_cast<unsigned char>(peek()))) get();
                if (peek() == '.') {
                    get();
                    while (std::isdigit(static_cast<unsigned char>(peek()))) get();
                }
                if (peek() == 'e' || peek() == 'E') {
                    get();
                    if (peek() == '+' || peek() == '-') get();
                    while (std::isdigit(static_cast<unsigned char>(peek()))) get();
                }
                std::string_view numStr = text_.substr(start, pos_ - start);
                if (numStr.empty() || numStr == "-") {
                    return std::unexpected(error("invalid number format"));
                }
                double val = 0.0;
                auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), val);
                if (ec != std::errc{}) {
                    return std::unexpected(error(std::format("failed to parse numeric value \"{}\"", numStr)));
                }
                return val;
            }

            std::expected<JsonArray, std::string> parseArray() {
                if (get() != '[') return std::unexpected(error("expected '['"));
                skipWhitespace();
                JsonArray arr;
                if (peek() == ']') {
                    get();
                    return arr;
                }
                while (true) {
                    skipWhitespace();
                    auto val = parseValue();
                    if (!val) return std::unexpected(val.error());
                    arr.push_back(std::move(*val));

                    skipWhitespace();
                    char c = peek();
                    if (c == ']') {
                        get();
                        return arr;
                    }
                    if (c == ',') {
                        get();
                    } else {
                        return std::unexpected(error("expected ',' or ']' in array"));
                    }
                }
            }

            std::expected<JsonObject, std::string> parseObject() {
                if (get() != '{') return std::unexpected(error("expected '{'"));
                skipWhitespace();
                JsonObject obj;
                if (peek() == '}') {
                    get();
                    return obj;
                }
                while (true) {
                    skipWhitespace();
                    if (peek() != '"') {
                        return std::unexpected(error("expected string key in object"));
                    }
                    auto key = parseString();
                    if (!key) return std::unexpected(key.error());

                    skipWhitespace();
                    if (get() != ':') {
                        return std::unexpected(error("expected ':' after key"));
                    }

                    skipWhitespace();
                    auto val = parseValue();
                    if (!val) return std::unexpected(val.error());
                    obj.emplace(std::move(*key), std::move(*val));

                    skipWhitespace();
                    char c = peek();
                    if (c == '}') {
                        get();
                        return obj;
                    }
                    if (c == ',') {
                        get();
                    } else {
                        return std::unexpected(error("expected ',' or '}' in object"));
                    }
                }
            }

        public:
            explicit JsonParser(std::string_view text) : text_(text) {}

            std::expected<JsonValue, std::string> parseValue() {
                skipWhitespace();
                if (atEnd()) return std::unexpected(error("unexpected end of input"));
                char c = peek();
                if (c == '{') {
                    auto obj = parseObject();
                    if (!obj) return std::unexpected(obj.error());
                    return JsonValue(std::move(*obj));
                }
                if (c == '[') {
                    auto arr = parseArray();
                    if (!arr) return std::unexpected(arr.error());
                    return JsonValue(std::move(*arr));
                }
                if (c == '"') {
                    auto str = parseString();
                    if (!str) return std::unexpected(str.error());
                    return JsonValue(std::move(*str));
                }
                if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
                    auto num = parseNumber();
                    if (!num) return std::unexpected(num.error());
                    return JsonValue(*num);
                }
                if (text_.substr(pos_).starts_with("true")) {
                    pos_ += 4;
                    return JsonValue(true);
                }
                if (text_.substr(pos_).starts_with("false")) {
                    pos_ += 5;
                    return JsonValue(false);
                }
                if (text_.substr(pos_).starts_with("null")) {
                    pos_ += 4;
                    return JsonValue(nullptr);
                }
                return std::unexpected(error(std::format("unexpected character '{}'", c)));
            }
        };

    } // anonymous namespace

    std::expected<JsonValue, std::string> parseJson(std::string_view jsonText) {
        JsonParser parser(jsonText);
        return parser.parseValue();
    }

} // namespace IO

