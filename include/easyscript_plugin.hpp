#pragma once

#include "value.hpp"
#include "object.hpp"
#include "vm.hpp"
#include "objects/obj_module.hpp"
#include "objects/obj_native_fn.hpp"
#include "objects/obj_string.hpp"

#if defined(_WIN32) || defined(__CYGWIN__)
  #define EASYSCRIPT_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define EASYSCRIPT_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// 外掛函式進入點簽章：
// bool easyscript_module_init(VM* vm, ObjModule* mod);
using EasyScriptModuleInitFn = bool (*)(VM* vm, ObjModule* mod);

// 標準模組初始化進入點宣告巨集
#define EASYSCRIPT_MODULE_INIT \
    extern "C" EASYSCRIPT_PLUGIN_EXPORT bool easyscript_module_init(VM* vm, ObjModule* mod)

// 具名模組初始化進入點宣告巨集 (如 easyscript_module_init_sqlite)
#define EASYSCRIPT_NAMED_MODULE_INIT(name) \
    extern "C" EASYSCRIPT_PLUGIN_EXPORT bool easyscript_module_init_##name(VM* vm, ObjModule* mod)
