#include "easyscript_plugin.hpp"
#include <iostream>

static VM* s_vm = nullptr;

static Value plugin_add(int argCount, Value* args) {
    if (argCount < 2) return Value(int64_t(0));
    int64_t a = args[0].isInt() ? *args[0].asInt() : 0;
    int64_t b = args[1].isInt() ? *args[1].asInt() : 0;
    return Value(a + b);
}

static Value plugin_multiply(int argCount, Value* args) {
    if (argCount < 2) return Value(int64_t(0));
    int64_t a = args[0].isInt() ? *args[0].asInt() : 0;
    int64_t b = args[1].isInt() ? *args[1].asInt() : 0;
    return Value(a * b);
}

static Value plugin_greet(int argCount, Value* args) {
    std::string name = "EasyScript";
    if (argCount >= 1 && args[0].isObject() && s_vm) {
        auto* str = s_vm->getHeap().getAs<ObjString>(*args[0].asObject());
        if (str) name = str->text;
    }
    std::string msg = "Hello, " + name + "! (from Native C++ Dynamic Library)";
    if (s_vm) {
        GcHandle h = s_vm->getHeap().allocateString(std::move(msg));
        return Value(h);
    }
    return Value();
}

EASYSCRIPT_MODULE_INIT {
    s_vm = vm;
    vm->defineModuleNative(mod, "add", plugin_add, 2);
    vm->defineModuleNative(mod, "multiply", plugin_multiply, 2);
    vm->defineModuleNative(mod, "greet", plugin_greet, 1);
    vm->defineModuleValue(mod, "PLUGIN_VERSION", Value(int64_t(100)));
    return true;
}
