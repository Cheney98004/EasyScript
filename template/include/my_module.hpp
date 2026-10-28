#pragma once

#include "easyscript_plugin.hpp"
#include "objects/obj_array.hpp"
#include "objects/obj_map.hpp"
#include <string>

namespace my_module {

// 模組元資料常數
constexpr const char* MODULE_NAME = "my_module";
constexpr int64_t MODULE_VERSION = 100; // v1.0.0

// 工具函式：安全提取 EasyScript Value 為 std::string
std::string valueToString(VM* vm, const Value& v);

// 原生導出函式原型 (符合 NativeFn: Value (*)(int argCount, Value* args))
Value add(int argCount, Value* args);
Value multiply(int argCount, Value* args);
Value greet(int argCount, Value* args);
Value makeRange(int argCount, Value* args);
Value getInfo(int argCount, Value* args);

} // namespace my_module
