#pragma once

#include "gc_object.hpp"
#include <string>
#include <vector>
#include <cstdint>

class ObjFunction final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Function;
    std::string name;
    int arity{0};
    std::vector<Value> defaultValues;
    std::vector<uint8_t> bytecode;
    std::vector<Value> constants;
    int upvalueCount{0};

    // JIT 編譯快取與熱點計數
    void* nativeCode{nullptr};
    uint32_t invocationCount{0};
    uint32_t backedgeCount{0};
    bool jitFailed{false};

    // P1 + P2: Bytecode Inline Cache (IC)
    struct InlineCache {
        uint32_t classId{0};
        uint32_t slot{0};
        Value value{};
    };
    mutable std::vector<InlineCache> icTable;

    explicit ObjFunction(std::string name = "");

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
