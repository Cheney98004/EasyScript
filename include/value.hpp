#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <functional>
#include <type_traits>

struct GcHandle {
    uint32_t id{0};

    constexpr explicit GcHandle(uint32_t val = 0) noexcept : id(val) {}

    constexpr bool operator==(const GcHandle& other) const noexcept = default;
    constexpr auto operator<=>(const GcHandle& other) const noexcept = default;
};

struct NullVal {
    constexpr bool operator==(const NullVal&) const noexcept = default;
};

// =============================================================================
// NaN-Boxing Constants (IEEE-754 64-bit Quiet NaN Encoding)
// =============================================================================
// 在 64 位元 IEEE-754 浮點數架構中：
// - Bit 63: 符號位 (Sign)
// - Bits 62..52: 指數位 (Exponent, 全 1 即 0x7FF 時表示 NaN 或 Infinity)
// - Bit 51: Quiet NaN 識別位 (1 = QNaN)
// - Bits 50..48: 3 位元自定義類型標籤 (Tag, 提供 8 種標籤)
// - Bits 47..0: 48 位元資料負載 (Payload, 可存 48 位元有號整數或 32 位元 GcHandle)
//
// 任何標準浮點數（包括 NaN 運算結果）在存入時均將 NaN 正規化為 CANONICAL_NAN_BITS (0x7FF8...)，
// 其符號位必定為 0；
// 而所有非浮點標籤（Nil, Bool, Object, Int）的符號位 (Bit 63) 與指數+Quiet位皆強制為 1 (0xFFF8...)。
// 這保證了 (bits & 0xFFF8000000000000ULL) != 0xFFF8000000000000ULL 恆成立於所有 Double。
// =============================================================================
namespace nanbox {
    inline constexpr uint64_t CANONICAL_NAN_BITS = 0x7FF8000000000000ULL;
    inline constexpr uint64_t TAG_PREFIX         = 0xFFF8000000000000ULL;

    inline constexpr uint64_t TAG_NIL            = 0xFFF8000000000000ULL; // 標籤 000
    inline constexpr uint64_t TAG_BOOL           = 0xFFF9000000000000ULL; // 標籤 001
    inline constexpr uint64_t TAG_FALSE          = 0xFFF9000000000000ULL;
    inline constexpr uint64_t TAG_TRUE           = 0xFFF9000000000001ULL;
    inline constexpr uint64_t TAG_OBJ            = 0xFFFA000000000000ULL; // 標籤 010 (低 32 位元為 GcHandle.id)
    inline constexpr uint64_t TAG_INT            = 0xFFFB000000000000ULL; // 標籤 011 (低 48 位元為有號整數)

    inline constexpr uint64_t PAYLOAD_48_MASK    = 0x0000FFFFFFFFFFFFULL;
    inline constexpr uint64_t TAG_MASK           = 0xFFFF000000000000ULL;
}

class Value {
private:
    uint64_t bits{nanbox::TAG_NIL};

public:
    // -------------------------------------------------------------------------
    // 建構子 (Constructors)
    // -------------------------------------------------------------------------
    constexpr Value() noexcept : bits(nanbox::TAG_NIL) {}
    constexpr Value(NullVal) noexcept : bits(nanbox::TAG_NIL) {}

    constexpr Value(bool b) noexcept
        : bits(b ? nanbox::TAG_TRUE : nanbox::TAG_FALSE) {}

    constexpr Value(int64_t i) noexcept
        : bits(nanbox::TAG_INT | (static_cast<uint64_t>(i) & nanbox::PAYLOAD_48_MASK)) {}

    constexpr Value(int i) noexcept
        : Value(static_cast<int64_t>(i)) {}

    Value(double d) noexcept {
        if (std::isnan(d)) {
            bits = nanbox::CANONICAL_NAN_BITS;
        } else {
            std::memcpy(&bits, &d, sizeof(double));
        }
    }

    constexpr Value(GcHandle h) noexcept
        : bits(nanbox::TAG_OBJ | static_cast<uint64_t>(h.id)) {}

    // -------------------------------------------------------------------------
    // 型別判定 (Type Predicates) - 單指令或極低開銷位元操作
    // -------------------------------------------------------------------------
    [[nodiscard]] constexpr bool isNull() const noexcept {
        return bits == nanbox::TAG_NIL;
    }

    [[nodiscard]] constexpr bool isBool() const noexcept {
        return (bits & ~1ULL) == nanbox::TAG_BOOL;
    }

    [[nodiscard]] constexpr bool isInt() const noexcept {
        return (bits & nanbox::TAG_MASK) == nanbox::TAG_INT;
    }

    [[nodiscard]] bool isDouble() const noexcept {
        return (bits & nanbox::TAG_PREFIX) != nanbox::TAG_PREFIX;
    }

    [[nodiscard]] bool isNumber() const noexcept {
        return isInt() || isDouble();
    }

    [[nodiscard]] constexpr bool isObject() const noexcept {
        return (bits & nanbox::TAG_MASK) == nanbox::TAG_OBJ;
    }

    // -------------------------------------------------------------------------
    // 取值介面 (Getters)
    // -------------------------------------------------------------------------
    [[nodiscard]] std::optional<bool> asBool() const noexcept;
    [[nodiscard]] std::optional<int64_t> asInt() const noexcept;
    [[nodiscard]] std::optional<double> asDouble() const noexcept;
    [[nodiscard]] std::optional<double> asNumber() const noexcept;
    [[nodiscard]] std::optional<GcHandle> asObject() const noexcept;

    // 快速未檢查直接解包 (可用於內部已知型別熱路徑)
    [[nodiscard]] constexpr bool asBoolUnchecked() const noexcept {
        return (bits & 1ULL) != 0;
    }

    [[nodiscard]] constexpr int64_t asIntUnchecked() const noexcept {
        return static_cast<int64_t>(bits << 16) >> 16;
    }

    [[nodiscard]] double asDoubleUnchecked() const noexcept {
        double d;
        std::memcpy(&d, &bits, sizeof(double));
        return d;
    }

    [[nodiscard]] constexpr GcHandle asObjectUnchecked() const noexcept {
        return GcHandle(static_cast<uint32_t>(bits & 0xFFFFFFFFULL));
    }

    // -------------------------------------------------------------------------
    // 真值判定與型別名稱 (Truthiness & Type Name)
    // -------------------------------------------------------------------------
    [[nodiscard]] constexpr bool isFalsey() const noexcept {
        return bits == nanbox::TAG_NIL || bits == nanbox::TAG_FALSE;
    }

    [[nodiscard]] constexpr bool isTruthy() const noexcept {
        return !isFalsey();
    }

    [[nodiscard]] std::string_view typeName() const noexcept;

    // -------------------------------------------------------------------------
    // 等價比對 (Equality)
    // -------------------------------------------------------------------------
    bool operator==(const Value& other) const noexcept;
    bool operator!=(const Value& other) const noexcept { return !(*this == other); }

    // -------------------------------------------------------------------------
    // 底層位元存取 (Raw Bit Access)
    // -------------------------------------------------------------------------
    [[nodiscard]] constexpr uint64_t rawBits() const noexcept { return bits; }
    static constexpr Value fromRawBits(uint64_t raw) noexcept {
        Value v;
        v.bits = raw;
        return v;
    }
};

static_assert(sizeof(Value) == 8, "Value must be exactly 8 bytes (NaN-boxed)!");
static_assert(std::is_trivially_copyable_v<Value>, "Value must be trivially copyable!");

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
