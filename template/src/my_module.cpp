/**
 * ============================================================================
 * EasyScript C++ Native Extension Module Implementation
 * ============================================================================
 *
 * 此檔案為 EasyScript C++ 原生模組範本。展示了：
 *  1. 數字與算術運算傳參與回傳
 *  2. 字串參數讀取與 GC 堆字串動態分配
 *  3. 陣列 (Array) 與映射 (Map/Dictionary) 的 GC 分配與構建
 *  4. 參數型別檢查、錯誤回報 (jitRuntimeError)
 *  5. 模組常數與原生函式的註冊與導出 (EASYSCRIPT_MODULE_INIT)
 */

#include "my_module.hpp"
#include <iostream>
#include <vector>
#include <unordered_map>

// 暫存 VM 指針以供本模組記憶體分配與型別檢查使用
static VM* s_vm = nullptr;

namespace my_module {

std::string valueToString(VM* vm, const Value& v) {
    if (v.isObject() && vm) {
        auto* str = vm->getHeap().getAs<ObjString>(*v.asObject());
        if (str) return str->text;
    }
    return vm ? vm->valueToString(v) : "";
}

/**
 * 範例 1: 基礎整數/數值加法
 * EasyScript 簽章: add(a: int, b: int): int
 */
Value add(int argCount, Value* args) {
    if (argCount < 2) {
        if (s_vm) s_vm->jitRuntimeError("my_module.add expects 2 arguments.");
        return Value(int64_t(0));
    }

    int64_t a = args[0].isInt() ? *args[0].asInt() : 0;
    int64_t b = args[1].isInt() ? *args[1].asInt() : 0;
    return Value(a + b);
}

/**
 * 範例 2: 基礎數值乘法 (支援浮點數與整數)
 * EasyScript 簽章: multiply(a: number, b: number): number
 */
Value multiply(int argCount, Value* args) {
    if (argCount < 2) {
        if (s_vm) s_vm->jitRuntimeError("my_module.multiply expects 2 arguments.");
        return Value(0.0);
    }

    double a = args[0].isNumber() ? *args[0].asNumber() : 0.0;
    double b = args[1].isNumber() ? *args[1].asNumber() : 0.0;
    return Value(a * b);
}

/**
 * 範例 3: 字串處理與回傳動態 GC 託管字串
 * EasyScript 簽章: greet(name: string): string
 */
Value greet(int argCount, Value* args) {
    std::string name = "World";
    if (argCount >= 1 && s_vm) {
        name = valueToString(s_vm, args[0]);
    }

    std::string message = "Hello, " + name + "! Welcome to EasyScript C++ Native Extension.";

    if (s_vm) {
        GcHandle h = s_vm->getHeap().allocateString(std::move(message));
        return Value(h);
    }
    return Value();
}

/**
 * 範例 4: 構建並回傳動態陣列 (Array)
 * EasyScript 簽章: makeRange(count: int): Array<int>
 */
Value makeRange(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();

    int64_t count = args[0].isInt() ? *args[0].asInt() : 0;
    if (count < 0) count = 0;

    std::vector<Value> elements;
    elements.reserve(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        elements.push_back(Value(i));
    }

    GcHandle arrHandle = s_vm->getHeap().allocateArray(std::move(elements));
    return Value(arrHandle);
}

/**
 * 範例 5: 構建並回傳映射字典 (Map)
 * EasyScript 簽章: getInfo(): Map
 */
Value getInfo(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    if (!s_vm) return Value();

    std::unordered_map<Value, Value> data;
    
    // 鍵名需透過 Heap allocateString 建立
    GcHandle keyModule = s_vm->getHeap().allocateString("module");
    GcHandle valModule = s_vm->getHeap().allocateString(MODULE_NAME);
    data[Value(keyModule)] = Value(valModule);

    GcHandle keyVersion = s_vm->getHeap().allocateString("version");
    data[Value(keyVersion)] = Value(MODULE_VERSION);

    GcHandle keyAuthor = s_vm->getHeap().allocateString("language");
    GcHandle valAuthor = s_vm->getHeap().allocateString("EasyScript C++ Native Plugin");
    data[Value(keyAuthor)] = Value(valAuthor);

    GcHandle mapHandle = s_vm->getHeap().allocateMap(std::move(data));
    return Value(mapHandle);
}

} // namespace my_module

/**
 * ============================================================================
 * 模組進入點 (Module Initialization Entry Point)
 * ============================================================================
 *
 * EasyScript 載入動態庫 (.dll / .so / .dylib) 時，會尋找此匯出符號：
 *   bool easyscript_module_init(VM* vm, ObjModule* mod);
 *
 * 也可以使用 EASYSCRIPT_NAMED_MODULE_INIT(my_module) 來建立具名初始化器。
 */
EASYSCRIPT_MODULE_INIT {
    s_vm = vm;

    // 1. 註冊導出原生函式 (函式名, 函式指針, 預期參數個數；-1 代表可變參數)
    vm->defineModuleNative(mod, "add", my_module::add, 2);
    vm->defineModuleNative(mod, "multiply", my_module::multiply, 2);
    vm->defineModuleNative(mod, "greet", my_module::greet, 1);
    vm->defineModuleNative(mod, "makeRange", my_module::makeRange, 1);
    vm->defineModuleNative(mod, "getInfo", my_module::getInfo, 0);

    // 2. 註冊模組常數
    vm->defineModuleValue(mod, "VERSION", Value(my_module::MODULE_VERSION));
    vm->defineModuleValue(mod, "MODULE_NAME", Value(vm->getHeap().allocateString(my_module::MODULE_NAME)));

    return true;
}
