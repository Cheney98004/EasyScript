#pragma once

#include "../value.hpp"
#include <iostream>
#include <functional>
#include <string_view>
#include <cstdint>

enum class ObjType : uint8_t {
    String,         // 字串
    Array,          // 動態陣列
    Map,            // 鍵值對映射
    Range,          // 數值區間
    NativeFn,       // 原生 C++ 函式
    Function,       // 腳本函式 (Bytecode/AST)
    Closure,        // 閉包
    Upvalue,        // 捕獲變數
    Class,          // 類別元資料
    Instance,       // 類別實例
    BoundMethod,    // 綁定方法
    Module,         // 模組
};

std::string_view objTypeName(ObjType type) noexcept;

class GcObject {
public:
    ObjType type;
    bool marked{false};

    explicit GcObject(ObjType t) noexcept : type(t) {}
    virtual ~GcObject() = default;

    virtual void trace(const std::function<void(Value)>& /*visitor*/) const {}
    virtual void print(std::ostream& os) const = 0;

    template <typename T>
    bool is() const noexcept {
        return type == T::TYPE;
    }

    template <typename T>
    const T* as() const noexcept {
        return is<T>() ? static_cast<const T*>(this) : nullptr;
    }

    template <typename T>
    T* as() noexcept {
        return is<T>() ? static_cast<T*>(this) : nullptr;
    }
};

std::ostream& operator<<(std::ostream& os, const GcObject& obj);
