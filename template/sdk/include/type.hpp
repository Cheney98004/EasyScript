#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <iostream>

enum class TypeKind : uint8_t {
    ANY,
    NIL,
    INT,
    DOUBLE,
    BOOL,
    STRING,
    VOID,
    ARRAY,
    MAP,
    CLASS,
    FUNCTION
};

struct Type {
    TypeKind kind{TypeKind::ANY};
    std::string className{};
    std::shared_ptr<Type> elementType{nullptr};
    std::shared_ptr<Type> keyType{nullptr};
    std::shared_ptr<Type> valueType{nullptr};
    std::vector<Type> paramTypes{};
    std::shared_ptr<Type> returnType{nullptr};

    Type() = default;
    explicit Type(TypeKind k) noexcept : kind(k) {}

    static Type Any() noexcept { return Type{TypeKind::ANY}; }
    static Type Nil() noexcept { return Type{TypeKind::NIL}; }
    static Type Int() noexcept { return Type{TypeKind::INT}; }
    static Type Double() noexcept { return Type{TypeKind::DOUBLE}; }
    static Type Bool() noexcept { return Type{TypeKind::BOOL}; }
    static Type String() noexcept { return Type{TypeKind::STRING}; }
    static Type Void() noexcept { return Type{TypeKind::VOID}; }

    static Type Array(Type elem) {
        Type t{TypeKind::ARRAY};
        t.elementType = std::make_shared<Type>(std::move(elem));
        return t;
    }

    static Type Map(Type key, Type val) {
        Type t{TypeKind::MAP};
        t.keyType = std::make_shared<Type>(std::move(key));
        t.valueType = std::make_shared<Type>(std::move(val));
        return t;
    }

    static Type Class(std::string name) {
        Type t{TypeKind::CLASS};
        t.className = std::move(name);
        return t;
    }

    static Type Function(std::vector<Type> params, Type ret) {
        Type t{TypeKind::FUNCTION};
        t.paramTypes = std::move(params);
        t.returnType = std::make_shared<Type>(std::move(ret));
        return t;
    }

    std::string toString() const {
        switch (kind) {
            case TypeKind::ANY: return "any";
            case TypeKind::NIL: return "nil";
            case TypeKind::INT: return "int";
            case TypeKind::DOUBLE: return "double";
            case TypeKind::BOOL: return "bool";
            case TypeKind::STRING: return "string";
            case TypeKind::VOID: return "void";
            case TypeKind::ARRAY: return (elementType ? elementType->toString() : "any") + "[]";
            case TypeKind::MAP: return "Map<" + (keyType ? keyType->toString() : "any") + ", " + (valueType ? valueType->toString() : "any") + ">";
            case TypeKind::CLASS: return className.empty() ? "Object" : className;
            case TypeKind::FUNCTION: return "fn(...)";
        }
        return "unknown";
    }

    bool operator==(const Type& other) const {
        if (kind != other.kind) return false;
        if (kind == TypeKind::CLASS) return className == other.className;
        if (kind == TypeKind::ARRAY) {
            if (!elementType || !other.elementType) return true;
            return *elementType == *other.elementType;
        }
        if (kind == TypeKind::MAP) {
            if (!keyType || !other.keyType || !valueType || !other.valueType) return true;
            return (*keyType == *other.keyType) && (*valueType == *other.valueType);
        }
        return true;
    }

    bool operator!=(const Type& other) const { return !(*this == other); }
};

inline bool isAssignable(const Type& target, const Type& source, const std::unordered_map<std::string, std::string>& classHierarchy = {}) {
    if (target.kind == TypeKind::ANY || source.kind == TypeKind::ANY) return true;
    if (target.kind == TypeKind::DOUBLE && source.kind == TypeKind::INT) return true;
    if (target.kind == TypeKind::ARRAY && source.kind == TypeKind::ARRAY) {
        if (!target.elementType || !source.elementType) return true;
        return isAssignable(*target.elementType, *source.elementType, classHierarchy);
    }
    if (target.kind == TypeKind::MAP && source.kind == TypeKind::MAP) {
        if (!target.keyType || !source.keyType || !target.valueType || !source.valueType) return true;
        return isAssignable(*target.keyType, *source.keyType, classHierarchy) &&
               isAssignable(*target.valueType, *source.valueType, classHierarchy);
    }
    if (target.kind == TypeKind::CLASS && source.kind == TypeKind::CLASS) {
        if (target.className == source.className) return true;
        std::string cur = source.className;
        while (!cur.empty()) {
            auto it = classHierarchy.find(cur);
            if (it != classHierarchy.end()) {
                if (it->second == target.className) return true;
                cur = it->second;
            } else {
                break;
            }
        }
        return false;
    }
    return target == source;
}
