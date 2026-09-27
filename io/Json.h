#ifndef EQUITIESSCREENER_IO_JSON_H
#define EQUITIESSCREENER_IO_JSON_H

#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace IO {

    class JsonValue;
    using JsonObject = std::map<std::string, JsonValue>;
    using JsonArray  = std::vector<JsonValue>;

    class JsonValue {
    public:
        using ValueVariant = std::variant<
            std::nullptr_t,
            bool,
            double,
            std::string,
            JsonArray,
            JsonObject
        >;

    private:
        ValueVariant value_{nullptr};

    public:
        JsonValue() = default;
        JsonValue(std::nullptr_t) : value_(nullptr) {}
        JsonValue(bool b) : value_(b) {}
        JsonValue(double d) : value_(d) {}
        JsonValue(int i) : value_(static_cast<double>(i)) {}
        JsonValue(long l) : value_(static_cast<double>(l)) {}
        JsonValue(std::string s) : value_(std::move(s)) {}
        JsonValue(std::string_view sv) : value_(std::string(sv)) {}
        JsonValue(const char* s) : value_(std::string(s)) {}
        JsonValue(JsonArray arr) : value_(std::move(arr)) {}
        JsonValue(JsonObject obj) : value_(std::move(obj)) {}

        [[nodiscard]] bool isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }
        [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(value_); }
        [[nodiscard]] bool isNumber() const noexcept { return std::holds_alternative<double>(value_); }
        [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(value_); }
        [[nodiscard]] bool isArray() const noexcept { return std::holds_alternative<JsonArray>(value_); }
        [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<JsonObject>(value_); }

        [[nodiscard]] std::optional<bool> asBool() const noexcept {
            if (const auto* v = std::get_if<bool>(&value_)) return *v;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<double> asDouble() const noexcept {
            if (const auto* v = std::get_if<double>(&value_)) return *v;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<float> asFloat() const noexcept {
            if (const auto* v = std::get_if<double>(&value_)) return static_cast<float>(*v);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<int> asInt() const noexcept {
            if (const auto* v = std::get_if<double>(&value_)) return static_cast<int>(*v);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string_view> asString() const noexcept {
            if (const auto* v = std::get_if<std::string>(&value_)) return *v;
            return std::nullopt;
        }

        [[nodiscard]] const JsonArray* asArray() const noexcept {
            return std::get_if<JsonArray>(&value_);
        }

        [[nodiscard]] const JsonObject* asObject() const noexcept {
            return std::get_if<JsonObject>(&value_);
        }

        [[nodiscard]] bool contains(const std::string& key) const noexcept {
            if (const auto* obj = asObject()) {
                return obj->find(key) != obj->end();
            }
            return false;
        }

        [[nodiscard]] const JsonValue* get(const std::string& key) const noexcept {
            if (const auto* obj = asObject()) {
                auto it = obj->find(key);
                if (it != obj->end()) return &it->second;
            }
            return nullptr;
        }

        [[nodiscard]] std::size_t size() const noexcept {
            if (const auto* arr = asArray()) return arr->size();
            if (const auto* obj = asObject()) return obj->size();
            return 0;
        }

        [[nodiscard]] const JsonValue& operator[](std::size_t index) const {
            static const JsonValue nullVal{nullptr};
            if (const auto* arr = asArray()) {
                if (index < arr->size()) return (*arr)[index];
            }
            return nullVal;
        }

        [[nodiscard]] const JsonValue& operator[](const std::string& key) const {
            static const JsonValue nullVal{nullptr};
            if (const auto* p = get(key)) return *p;
            return nullVal;
        }
    };

    /// Parse a JSON string into a JsonValue.
    /// Returns an error message on malformed JSON.
    [[nodiscard]] std::expected<JsonValue, std::string>
    parseJson(std::string_view jsonText);

} // namespace IO

#endif // EQUITIESSCREENER_IO_JSON_H

