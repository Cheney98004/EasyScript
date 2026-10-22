#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdint>
#include <algorithm>

namespace lsp {

enum class JsonType {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

class Json {
public:
    JsonType type{JsonType::Null};
    bool boolVal{false};
    double numVal{0.0};
    int64_t intVal{0};
    bool isInteger{false};
    std::string strVal;
    std::vector<Json> arrVal;
    std::map<std::string, Json> objVal;

    Json() = default;
    Json(std::nullptr_t) : type(JsonType::Null) {}
    Json(bool b) : type(JsonType::Bool), boolVal(b) {}
    Json(int n) : type(JsonType::Number), numVal(n), intVal(n), isInteger(true) {}
    Json(int64_t n) : type(JsonType::Number), numVal(static_cast<double>(n)), intVal(n), isInteger(true) {}
    Json(uint32_t n) : type(JsonType::Number), numVal(n), intVal(n), isInteger(true) {}
    Json(size_t n) : type(JsonType::Number), numVal(static_cast<double>(n)), intVal(static_cast<int64_t>(n)), isInteger(true) {}
    Json(double d) : type(JsonType::Number), numVal(d), intVal(static_cast<int64_t>(d)), isInteger(false) {}
    Json(const char* s) : type(JsonType::String), strVal(s ? s : "") {}
    Json(std::string s) : type(JsonType::String), strVal(std::move(s)) {}
    Json(std::vector<Json> a) : type(JsonType::Array), arrVal(std::move(a)) {}
    Json(std::map<std::string, Json> o) : type(JsonType::Object), objVal(std::move(o)) {}

    static Json array() {
        Json j;
        j.type = JsonType::Array;
        return j;
    }

    static Json object() {
        Json j;
        j.type = JsonType::Object;
        return j;
    }

    bool isNull() const noexcept { return type == JsonType::Null; }
    bool isBool() const noexcept { return type == JsonType::Bool; }
    bool isNumber() const noexcept { return type == JsonType::Number; }
    bool isString() const noexcept { return type == JsonType::String; }
    bool isArray() const noexcept { return type == JsonType::Array; }
    bool isObject() const noexcept { return type == JsonType::Object; }

    const std::string& asString() const { return strVal; }
    int64_t asInt() const { return intVal; }
    bool asBool() const { return boolVal; }

    bool contains(const std::string& key) const {
        if (type != JsonType::Object) return false;
        return objVal.find(key) != objVal.end();
    }

    Json& operator[](const std::string& key) {
        if (type != JsonType::Object) {
            type = JsonType::Object;
        }
        return objVal[key];
    }

    const Json& operator[](const std::string& key) const {
        static const Json nullJson;
        if (type != JsonType::Object) return nullJson;
        auto it = objVal.find(key);
        return (it != objVal.end()) ? it->second : nullJson;
    }

    Json& operator[](size_t index) {
        if (type != JsonType::Array) {
            type = JsonType::Array;
        }
        if (index >= arrVal.size()) {
            arrVal.resize(index + 1);
        }
        return arrVal[index];
    }

    const Json& operator[](size_t index) const {
        static const Json nullJson;
        if (type != JsonType::Array || index >= arrVal.size()) return nullJson;
        return arrVal[index];
    }

    void push_back(Json val) {
        if (type != JsonType::Array) {
            type = JsonType::Array;
        }
        arrVal.push_back(std::move(val));
    }

    std::string dump() const {
        std::ostringstream ss;
        serialize(ss);
        return ss.str();
    }

private:
    void serialize(std::ostream& os) const {
        switch (type) {
            case JsonType::Null: os << "null"; break;
            case JsonType::Bool: os << (boolVal ? "true" : "false"); break;
            case JsonType::Number:
                if (isInteger) os << intVal;
                else os << numVal;
                break;
            case JsonType::String: {
                os << '"';
                for (char c : strVal) {
                    switch (c) {
                        case '"': os << "\\\""; break;
                        case '\\': os << "\\\\"; break;
                        case '\b': os << "\\b"; break;
                        case '\f': os << "\\f"; break;
                        case '\n': os << "\\n"; break;
                        case '\r': os << "\\r"; break;
                        case '\t': os << "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20) {
                                char buf[8];
                                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                                os << buf;
                            } else {
                                os << c;
                            }
                            break;
                    }
                }
                os << '"';
                break;
            }
            case JsonType::Array: {
                os << '[';
                for (size_t i = 0; i < arrVal.size(); ++i) {
                    if (i > 0) os << ',';
                    arrVal[i].serialize(os);
                }
                os << ']';
                break;
            }
            case JsonType::Object: {
                os << '{';
                bool first = true;
                for (const auto& [k, v] : objVal) {
                    if (!first) os << ',';
                    first = false;
                    os << '"';
                    for (char c : k) {
                        if (c == '"') os << "\\\"";
                        else if (c == '\\') os << "\\\\";
                        else os << c;
                    }
                    os << "\":";
                    v.serialize(os);
                }
                os << '}';
                break;
            }
        }
    }
};

// -----------------------------------------------------------------------------
// 輕量 JSON 解析器 (Lightweight JSON Parser)
// -----------------------------------------------------------------------------
class JsonParser {
public:
    static Json parse(const std::string& input) {
        size_t idx = 0;
        skipWhitespace(input, idx);
        return parseValue(input, idx);
    }

private:
    static void skipWhitespace(const std::string& s, size_t& idx) {
        while (idx < s.size() && (std::isspace(static_cast<unsigned char>(s[idx])))) {
            idx++;
        }
    }

    static Json parseValue(const std::string& s, size_t& idx) {
        skipWhitespace(s, idx);
        if (idx >= s.size()) return Json();

        char c = s[idx];
        if (c == 'n') return parseNull(s, idx);
        if (c == 't' || c == 'f') return parseBool(s, idx);
        if (c == '"') return parseString(s, idx);
        if (c == '[') return parseArray(s, idx);
        if (c == '{') return parseObject(s, idx);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(s, idx);

        return Json();
    }

    static Json parseNull(const std::string& s, size_t& idx) {
        if (s.compare(idx, 4, "null") == 0) {
            idx += 4;
            return Json();
        }
        return Json();
    }

    static Json parseBool(const std::string& s, size_t& idx) {
        if (s.compare(idx, 4, "true") == 0) {
            idx += 4;
            return Json(true);
        }
        if (s.compare(idx, 5, "false") == 0) {
            idx += 5;
            return Json(false);
        }
        return Json(false);
    }

    static Json parseNumber(const std::string& s, size_t& idx) {
        size_t start = idx;
        if (s[idx] == '-') idx++;
        bool isFloat = false;
        while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) idx++;
        if (idx < s.size() && s[idx] == '.') {
            isFloat = true;
            idx++;
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) idx++;
        }
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            isFloat = true;
            idx++;
            if (idx < s.size() && (s[idx] == '+' || s[idx] == '-')) idx++;
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) idx++;
        }
        std::string numStr = s.substr(start, idx - start);
        try {
            if (isFloat) return Json(std::stod(numStr));
            return Json(static_cast<int64_t>(std::stoll(numStr)));
        } catch (...) {
            return Json(0);
        }
    }

    static Json parseString(const std::string& s, size_t& idx) {
        idx++; // skip '"'
        std::string result;
        while (idx < s.size()) {
            char c = s[idx++];
            if (c == '"') break;
            if (c == '\\' && idx < s.size()) {
                char next = s[idx++];
                switch (next) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        if (idx + 4 <= s.size()) {
                            std::string hex = s.substr(idx, 4);
                            idx += 4;
                            try {
                                uint32_t codepoint = std::stoul(hex, nullptr, 16);
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
                            } catch (...) {}
                        }
                        break;
                    }
                    default: result.push_back(next); break;
                }
            } else {
                result.push_back(c);
            }
        }
        return Json(std::move(result));
    }

    static Json parseArray(const std::string& s, size_t& idx) {
        idx++; // skip '['
        Json arr = Json::array();
        skipWhitespace(s, idx);
        if (idx < s.size() && s[idx] == ']') {
            idx++;
            return arr;
        }
        while (idx < s.size()) {
            arr.push_back(parseValue(s, idx));
            skipWhitespace(s, idx);
            if (idx < s.size() && s[idx] == ',') {
                idx++;
                skipWhitespace(s, idx);
            } else if (idx < s.size() && s[idx] == ']') {
                idx++;
                break;
            } else {
                idx++;
            }
        }
        return arr;
    }

    static Json parseObject(const std::string& s, size_t& idx) {
        idx++; // skip '{'
        Json obj = Json::object();
        skipWhitespace(s, idx);
        if (idx < s.size() && s[idx] == '}') {
            idx++;
            return obj;
        }
        while (idx < s.size()) {
            skipWhitespace(s, idx);
            if (idx >= s.size() || s[idx] != '"') break;
            Json keyJson = parseString(s, idx);
            std::string key = keyJson.asString();
            skipWhitespace(s, idx);
            if (idx < s.size() && s[idx] == ':') idx++;
            skipWhitespace(s, idx);
            obj[key] = parseValue(s, idx);
            skipWhitespace(s, idx);
            if (idx < s.size() && s[idx] == ',') {
                idx++;
                skipWhitespace(s, idx);
            } else if (idx < s.size() && s[idx] == '}') {
                idx++;
                break;
            } else {
                idx++;
            }
        }
        return obj;
    }
};

} // namespace lsp
