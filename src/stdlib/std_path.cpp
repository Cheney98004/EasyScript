#include "easyscript_plugin.hpp"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

static VM* s_vm = nullptr;

static std::string valueToStr(const Value& v) {
    if (v.isObject() && s_vm) {
        auto* str = s_vm->getHeap().getAs<ObjString>(*v.asObject());
        if (str) return str->text;
    }
    return s_vm ? s_vm->valueToString(v) : "";
}

static std::string normalizeSlashes(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

// path.join(...parts): string
static Value path_join(int argCount, Value* args) {
    if (!s_vm) return Value();
    fs::path p;
    for (int i = 0; i < argCount; ++i) {
        std::string part = valueToStr(args[i]);
        if (!part.empty()) {
            p /= fs::path(part);
        }
    }
    std::string res = normalizeSlashes(p.lexically_normal().string());
    return Value(s_vm->getHeap().allocateString(std::move(res)));
}

// path.dirname(path: string): string
static Value path_dirname(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string p = valueToStr(args[0]);
    std::string parent = normalizeSlashes(fs::path(p).parent_path().string());
    if (parent.empty()) parent = ".";
    return Value(s_vm->getHeap().allocateString(std::move(parent)));
}

// path.basename(path: string): string
static Value path_basename(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string p = valueToStr(args[0]);
    std::string fname = fs::path(p).filename().string();
    return Value(s_vm->getHeap().allocateString(std::move(fname)));
}

// path.extname(path: string): string
static Value path_extname(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string p = valueToStr(args[0]);
    std::string ext = fs::path(p).extension().string();
    return Value(s_vm->getHeap().allocateString(std::move(ext)));
}

// path.isAbsolute(path: string): bool
static Value path_isAbsolute(int argCount, Value* args) {
    if (argCount < 1) return Value(false);
    std::string p = valueToStr(args[0]);
    return Value(fs::path(p).is_absolute());
}

// path.resolve(...parts): string
static Value path_resolve(int argCount, Value* args) {
    if (!s_vm) return Value();
    fs::path p = fs::current_path();
    for (int i = 0; i < argCount; ++i) {
        std::string part = valueToStr(args[i]);
        if (!part.empty()) {
            p /= fs::path(part);
        }
    }
    std::error_code ec;
    fs::path absPath = fs::absolute(p, ec);
    std::string res = normalizeSlashes(absPath.lexically_normal().string());
    return Value(s_vm->getHeap().allocateString(std::move(res)));
}

// path.normalize(path: string): string
static Value path_normalize(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string p = valueToStr(args[0]);
    std::string res = normalizeSlashes(fs::path(p).lexically_normal().string());
    return Value(s_vm->getHeap().allocateString(std::move(res)));
}

EASYSCRIPT_MODULE_INIT {
    s_vm = vm;
    vm->defineModuleNative(mod, "join", path_join, -1);
    vm->defineModuleNative(mod, "dirname", path_dirname, 1);
    vm->defineModuleNative(mod, "basename", path_basename, 1);
    vm->defineModuleNative(mod, "extname", path_extname, 1);
    vm->defineModuleNative(mod, "isAbsolute", path_isAbsolute, 1);
    vm->defineModuleNative(mod, "resolve", path_resolve, -1);
    vm->defineModuleNative(mod, "normalize", path_normalize, 1);
    return true;
}
