#include "value.hpp"

// -----------------------------------------------------------------------------
// Type Predicates
// -----------------------------------------------------------------------------
bool Value::isNull() const noexcept {
    return std::holds_alternative<NullVal>(data);
}

bool Value::isBool() const noexcept {
    return std::holds_alternative<bool>(data);
}

bool Value::isInt()  const noexcept {
    return std::holds_alternative<int64_t>(data);
}

bool Value::isDouble() const noexcept {
    return std::holds_alternative<double>(data);
}

bool Value::isNumber() const noexcept {
    return isInt() || isDouble();
}

bool Value::isObject() const noexcept { 
    return std::holds_alternative<GcHandle>(data);
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
std::optional<bool> Value::asBool() const noexcept {
    if (const auto* v = std::get_if<bool>(&data)) return *v;
    return std::nullopt;
}

std::optional<int64_t> Value::asInt() const noexcept {
    if (const auto* v = std::get_if<int64_t>(&data)) return *v;
    return std::nullopt;
}

std::optional<double> Value::asDouble() const noexcept {
    if (const auto* v = std::get_if<double>(&data)) return *v;
    return std::nullopt;
}

std::optional<double> Value::asNumber() const noexcept {
    if (const auto* i = std::get_if<int64_t>(&data)) return static_cast<double>(*i);
    if (const auto* f = std::get_if<double>(&data)) return *f;
    return std::nullopt;
}

std::optional<GcHandle> Value::asObject() const noexcept {
    if (const auto* h = std::get_if<GcHandle>(&data)) return *h;
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// Truthiness & Type Name
// -----------------------------------------------------------------------------
bool Value::isFalsey() const noexcept {
    if (isNull()) return true;
    if (const auto* b = std::get_if<bool>(&data)) return !(*b);
    return false;
}

bool Value::isTruthy() const noexcept {
    return !isFalsey();
}

std::string_view Value::typeName() const noexcept {
    return std::visit([](const auto& val) -> std::string_view {
        using T = std::decay_t<decltype(val)>;
        if constexpr      (std::is_same_v<T, NullVal>)  return "nil";
        else if constexpr (std::is_same_v<T, bool>)     return "bool";
        else if constexpr (std::is_same_v<T, int64_t>)  return "int";
        else if constexpr (std::is_same_v<T, double>)   return "float";
        else if constexpr (std::is_same_v<T, GcHandle>) return "object";
    }, data);
}

// -----------------------------------------------------------------------------
// Equality
// -----------------------------------------------------------------------------
bool Value::operator==(const Value& other) const noexcept {
    if (data.index() != other.data.index()) {
        return false;
    }

    return std::visit([&](const auto& a) -> bool {
        using T = std::decay_t<decltype(a)>;
        const auto& b = std::get<T>(other.data);

        if constexpr (std::is_same_v<T, double>) {
            if (std::isnan(a) && std::isnan(b)) {
                return true;
            }
            if (a == 0.0 && b == 0.0) {
                return true;
            }
            return a == b;
        } else {
            return a == b;
        }
    }, data);
}

bool Value::operator!=(const Value& other) const noexcept {
    return !(*this == other);
}

// -----------------------------------------------------------------------------
// Output Stream
// -----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& os, const Value& val) {
    std::visit([&os](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullVal>) {
            os << "nil";
        } else if constexpr (std::is_same_v<T, bool>) {
            os << (v ? "true" : "false");
        } else if constexpr (std::is_same_v<T, int64_t>) {
            os << v;
        } else if constexpr (std::is_same_v<T, double>) {
            os << v;
        } else if constexpr (std::is_same_v<T, GcHandle>) {
            os << "<object #" << v.id << ">";
        }
    }, val.raw());
    return os;
}

// -----------------------------------------------------------------------------
// std::hash 實作
// -----------------------------------------------------------------------------
namespace std {

size_t hash<GcHandle>::operator()(const GcHandle& h) const noexcept {
    return std::hash<uint32_t>{}(h.id);
}

size_t hash<Value>::operator()(const Value& val) const noexcept {
    size_t seed = std::hash<size_t>{}(val.raw().index());

    auto combine = [&seed](size_t h) {
        seed ^= h + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    };

    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullVal>) {
            // Null 不混入額外負載
        } else if constexpr (std::is_same_v<T, bool>) {
            combine(std::hash<bool>{}(v));
        } else if constexpr (std::is_same_v<T, int64_t>) {
            combine(std::hash<int64_t>{}(v));
        } else if constexpr (std::is_same_v<T, double>) {
            uint64_t bits = 0;
            if (std::isnan(v)) {
                constexpr double qnan = std::numeric_limits<double>::quiet_NaN();
                std::memcpy(&bits, &qnan, sizeof(double));
            } else if (v == 0.0) {
                constexpr double zero = 0.0;
                std::memcpy(&bits, &zero, sizeof(double));
            } else {
                std::memcpy(&bits, &v, sizeof(double));
            }
            combine(std::hash<uint64_t>{}(bits));
        } else if constexpr (std::is_same_v<T, GcHandle>) {
            combine(std::hash<GcHandle>{}(v));
        }
    }, val.raw());

    return seed;
}

} // namespace std