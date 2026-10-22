#pragma once

#include "value.hpp"
#include "object.hpp"
#include "heap.hpp"
#include "opcode.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>

enum class InterpretResult {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR
};

// 呼叫訊框 (CallFrame)
struct CallFrame {
    ObjClosure* closure{nullptr};
    ObjFunction* function{nullptr};
    const uint8_t* ip{nullptr};
    size_t slotOffset{0}; // 該訊框在運算元棧上的起始位置
};

// 例外捕捉處理器 (TryHandler)
struct TryHandler {
    size_t frameIndex{0};
    size_t stackSize{0};
    const uint8_t* catchIp{nullptr};
    const uint8_t* finallyIp{nullptr};
    bool inCatch{false};
    bool inFinally{false};
    Value pendingException;
};

class VM {
public:
    VM();
    ~VM() = default;

    // 解譯並執行原始碼
    InterpretResult interpret(std::string_view source, const std::string& modulePath = "<main>");

    // 執行已編譯之 ObjFunction
    InterpretResult execute(ObjFunction* fn, const std::string& modulePath = "<main>");

    // 模組載入與註冊
    ObjModule* loadModule(const std::string& path);

    // 註冊 C++ 原生函式至全域環境
    void defineNative(const std::string& name, NativeFn function, int arity = -1);

    // 存取 Heap
    Heap& getHeap() noexcept { return heap; }
    std::unordered_map<std::string, Value>& getGlobals() noexcept { return globals; }

    // 運算元棧輔助
    void push(Value value);
    Value pop();
    Value peek(int distance = 0) const;

    // 取得全域變數
    std::optional<Value> getGlobal(const std::string& name) const;

    // 將任意 Value（含堆積物件）轉為字串表示
    std::string valueToString(Value value);

    // JIT 呼叫與全域輔助介面
    bool callFromJit(Value* calleeSlot, uint8_t argCount);
    Value jitGetGlobal(const std::string& name);
    Value jitGetGlobal(const Value& nameVal);
    void jitSetGlobal(const std::string& name, Value val);
    void jitSetGlobal(const Value& nameVal, Value val);
    void jitDefineGlobal(const std::string& name, Value val);
    void jitDefineGlobal(const Value& nameVal, Value val);
    void jitRuntimeError(const std::string& msg);

private:
    Heap heap;
    std::vector<CallFrame> frames;
    std::vector<Value> stack;
    std::unordered_map<std::string, Value> globals;
    std::unordered_map<std::string, GcHandle> modules;
    ObjModule* currentModule{nullptr};

    void initBuiltinModules();
    InterpretResult run(size_t stopDepth = 0);
    void runtimeError(const char* format, ...);
    bool call(Value callee, uint8_t argCount);
    bool callValue(Value callee, uint8_t argCount);

    bool invokeArrayMethod(ObjArray* arr, const std::string& name, uint8_t argCount, Value receiver);
    bool invokeStringMethod(ObjString* str, const std::string& name, uint8_t argCount, Value receiver);
    bool invokeMapMethod(ObjMap* map, const std::string& name, uint8_t argCount, Value receiver);
    bool invokeRangeMethod(ObjRange* rng, const std::string& name, uint8_t argCount, Value receiver);

    std::vector<GcHandle> openUpvalues;
    std::vector<TryHandler> tryHandlers;

    GcHandle captureUpvalue(Value* local);
    void closeUpvalues(Value* last);
    InterpretResult handleThrow(Value exception);

    void resetStack();
};
