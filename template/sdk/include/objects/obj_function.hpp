#pragma once

#include "gc_object.hpp"
#include <string>
#include <vector>
#include <cstdint>

struct JitCallSite {
    uint32_t expectedHandleId{0};
    uint32_t expectedArity{0};
    void* nativeCode{nullptr};
};

struct JitGlobalSite {
    Value* cachedSlot{nullptr};
};

struct JitPropertySite {
    uint32_t cachedClassId{0};
    uint32_t slot{0};
};

struct JitInvokeSite {
    uint32_t cachedClassId{0};
    uint32_t vtableSlot{0};
    void* nativeCode{nullptr};
    Value cachedMethod{};
};

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
    mutable std::vector<JitCallSite> jitCallSites;
    mutable std::vector<JitGlobalSite> jitGlobalSites;
    mutable std::vector<JitPropertySite> jitPropertySites;
    mutable std::vector<JitInvokeSite> jitInvokeSites;

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
