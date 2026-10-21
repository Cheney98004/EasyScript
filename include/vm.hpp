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
    ObjFunction* function{nullptr};
    const uint8_t* ip{nullptr};
    size_t slotOffset{0}; // 該訊框在運算元棧上的起始位置
};

class VM {
public:
    VM();
    ~VM() = default;

    // 解譯並執行原始碼
    InterpretResult interpret(std::string_view source, const std::string& modulePath = "<main>");

    // 模組載入與註冊
    ObjModule* loadModule(const std::string& path);

    // 註冊 C++ 原生函式至全域環境
    void defineNative(const std::string& name, NativeFn function, int arity = -1);

    // 存取 Heap
    Heap& getHeap() noexcept { return heap; }

    // 運算元棧輔助
    void push(Value value);
    Value pop();
    Value peek(int distance = 0) const;

    // 取得全域變數
    std::optional<Value> getGlobal(const std::string& name) const;

    // 將任意 Value（含堆積物件）轉為字串表示
    std::string valueToString(Value value);

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

    void resetStack();
};
