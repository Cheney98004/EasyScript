#pragma once

#include "value.hpp"
#include <vector>
#include <cstdint>
#include <cstddef>
#include <utility>

class VM;
class ObjFunction;

enum class JitStatus : int64_t {
    OK = 0,
    BAILOUT = 1,
    ERROR = 2
};

// JIT 原生函式原型: 返回 JitStatus (0: OK, 1: BAILOUT, 2: ERROR)
using JitNativeFn = JitStatus (*)(VM* vm, Value* slots, Value* outResult);

// JIT 可執行記憶體管理器 (基於 Windows VirtualAlloc)
class JitMemoryManager {
public:
    static JitMemoryManager& instance();

    void* allocateExecutable(size_t size);
    void freeAll();

    ~JitMemoryManager();

private:
    JitMemoryManager() = default;
    std::vector<std::pair<void*, size_t>> allocations;
};

// JIT 編譯器核心
class JitCompiler {
public:
    // 檢查函式是否包含支援的指令集可供 JIT 編譯
    static bool canCompile(const ObjFunction* fn);

    // 將 ObjFunction 的字節碼編譯為 x86_64 原生機器碼
    static bool compile(ObjFunction* fn);
};
