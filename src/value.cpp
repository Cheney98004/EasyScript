#include "value.hpp"

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------
std::optional<bool> Value::asBool() const noexcept {
    if (isBool()) return asBoolUnchecked();
    return std::nullopt;
}

std::optional<int64_t> Value::asInt() const noexcept {
    if (isInt()) return asIntUnchecked();
    return std::nullopt;
}

std::optional<double> Value::asDouble() const noexcept {
    if (isDouble()) return asDoubleUnchecked();
    return std::nullopt;
}

std::optional<double> Value::asNumber() const noexcept {
    if (isInt()) return static_cast<double>(asIntUnchecked());
    if (isDouble()) return asDoubleUnchecked();
    return std::nullopt;
}

std::optional<GcHandle> Value::asObject() const noexcept {
    if (isObject()) return asObjectUnchecked();
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// Type Name
// -----------------------------------------------------------------------------
std::string_view Value::typeName() const noexcept {
    if (isNull())   return "nil";
    if (isBool())   return "bool";
    if (isInt())    return "int";
    if (isDouble()) return "float";
    if (isObject()) return "object";
    return "unknown";
}

// -----------------------------------------------------------------------------
// Equality
// -----------------------------------------------------------------------------
bool Value::operator==(const Value& other) const noexcept {
    if (isDouble() && other.isDouble()) {
        double a = asDoubleUnchecked();
        double b = other.asDoubleUnchecked();
        if (std::isnan(a) && std::isnan(b)) return true;
        if (a == 0.0 && b == 0.0) return true;
        return a == b;
    }
    return bits == other.bits;
}

// -----------------------------------------------------------------------------
// Output Stream
// -----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& os, const Value& val) {
    if (val.isNull()) {
        os << "nil";
    } else if (val.isBool()) {
        os << (val.asBoolUnchecked() ? "true" : "false");
    } else if (val.isInt()) {
        os << val.asIntUnchecked();
    } else if (val.isDouble()) {
        os << val.asDoubleUnchecked();
    } else if (val.isObject()) {
        os << "<object #" << val.asObjectUnchecked().id << ">";
    }
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
    if (val.isDouble()) {
        double d = val.asDoubleUnchecked();
        if (std::isnan(d)) {
            return std::hash<uint64_t>{}(nanbox::CANONICAL_NAN_BITS);
        }
        if (d == 0.0) {
            return std::hash<double>{}(0.0);
        }
        return std::hash<double>{}(d);
    }
    return std::hash<uint64_t>{}(val.rawBits());
}

} // namespace std
