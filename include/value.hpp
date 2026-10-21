#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <variant>
#include <optional>
#include <string_view>
#include <functional>

struct GcHandle {
    uint32_t id{0};

    constexpr explicit GcHandle(uint32_t val = 0) noexcept : id(val) {}

    constexpr bool operator==(const GcHandle& other) const noexcept = default;
    constexpr auto operator<=>(const GcHandle& other) const noexcept = default;
};

struct NullVal {
    constexpr bool operator==(const NullVal&) const noexcept = default;
};

class Value {
public:
    using Storage = std::variant<NullVal, bool, int64_t, double, GcHandle>;

private:
    Storage data{NullVal{}};

public:
    // Factory methods
    constexpr Value() noexcept : data(NullVal{}) {}
    constexpr Value(bool b) noexcept : data(b) {}
    constexpr Value(int64_t i) noexcept : data(i) {}
    constexpr Value(double d) noexcept : data(d) {}
    constexpr Value(GcHandle h) noexcept : data(h) {}

    // Type predicates
    bool isNull() const noexcept;
    bool isBool() const noexcept;
    bool isInt()  const noexcept;
    bool isDouble() const noexcept;
    bool isNumber() const noexcept;
    bool isObject() const noexcept;

    // Getters
    std::optional<bool> asBool() const noexcept;
    std::optional<int64_t> asInt() const noexcept;
    std::optional<double> asDouble() const noexcept;
    std::optional<double> asNumber() const noexcept;
    std::optional<GcHandle> asObject() const noexcept;

    // Truthiness
    bool isFalsey() const noexcept;
    bool isTruthy() const noexcept;

    std::string_view typeName() const noexcept;

    // Equality
    bool operator==(const Value& other) const noexcept;
    bool operator!=(const Value& other) const noexcept;

    const Storage& raw() const noexcept { return data; }
};

std::ostream& operator<<(std::ostream& os, const Value& val);

// -----------------------------------------------------------------------------
// std::hash 特化宣告
// -----------------------------------------------------------------------------
namespace std {

template <>
struct hash<GcHandle> {
    size_t operator()(const GcHandle& h) const noexcept;
};

template <>
struct hash<Value> {
    size_t operator()(const Value& val) const noexcept;
};

} // namespace std