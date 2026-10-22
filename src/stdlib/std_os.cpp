#include "easyscript_plugin.hpp"
#include <filesystem>
#include <cstdlib>
#include <thread>

namespace fs = std::filesystem;

static VM* s_vm = nullptr;

static std::string valueToStr(const Value& v) {
    if (v.isObject() && s_vm) {
        auto* str = s_vm->getHeap().getAs<ObjString>(*v.asObject());
        if (str) return str->text;
    }
    return s_vm ? s_vm->valueToString(v) : "";
}

// os.platform(): string
static Value os_platform(int, Value*) {
    if (!s_vm) return Value();
#if defined(_WIN32)
    return Value(s_vm->getHeap().allocateString("windows"));
#elif defined(__APPLE__)
    return Value(s_vm->getHeap().allocateString("macos"));
#else
    return Value(s_vm->getHeap().allocateString("linux"));
#endif
}

// os.arch(): string
static Value os_arch(int, Value*) {
    if (!s_vm) return Value();
#if defined(__x86_64__) || defined(_M_X64)
    return Value(s_vm->getHeap().allocateString("x86_64"));
#elif defined(__i386__) || defined(_M_IX86)
    return Value(s_vm->getHeap().allocateString("x86"));
#elif defined(__aarch64__) || defined(_M_ARM64)
    return Value(s_vm->getHeap().allocateString("arm64"));
#elif defined(__arm__) || defined(_M_ARM)
    return Value(s_vm->getHeap().allocateString("arm"));
#else
    return Value(s_vm->getHeap().allocateString("unknown"));
#endif
}

// os.getEnv(name: string): string | nil
static Value os_getEnv(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string name = valueToStr(args[0]);
    const char* val = std::getenv(name.c_str());
    if (!val) return Value();
    return Value(s_vm->getHeap().allocateString(val));
}

// os.setEnv(name: string, value: string): bool
static Value os_setEnv(int argCount, Value* args) {
    if (argCount < 2) return Value(false);
    std::string name = valueToStr(args[0]);
    std::string val = valueToStr(args[1]);
#if defined(_WIN32)
    int res = _putenv_s(name.c_str(), val.c_str());
    return Value(res == 0);
#else
    int res = setenv(name.c_str(), val.c_str(), 1);
    return Value(res == 0);
#endif
}

// os.cwd(): string
static Value os_cwd(int, Value*) {
    if (!s_vm) return Value();
    std::error_code ec;
    std::string cur = fs::current_path(ec).string();
    return Value(s_vm->getHeap().allocateString(std::move(cur)));
}

// os.chdir(dir: string): bool
static Value os_chdir(int argCount, Value* args) {
    if (argCount < 1) return Value(false);
    std::string dir = valueToStr(args[0]);
    std::error_code ec;
    fs::current_path(dir, ec);
    return Value(!ec);
}

// os.exec(command: string): int
static Value os_exec(int argCount, Value* args) {
    if (argCount < 1) return Value(int64_t(-1));
    std::string cmd = valueToStr(args[0]);
    int code = std::system(cmd.c_str());
    return Value(static_cast<int64_t>(code));
}

// os.cpuCount(): int
static Value os_cpuCount(int, Value*) {
    unsigned int count = std::thread::hardware_concurrency();
    return Value(static_cast<int64_t>(count > 0 ? count : 1));
}

// os.exit(code: int)
static Value os_exit(int argCount, Value* args) {
    int code = 0;
    if (argCount >= 1 && args[0].isInt()) {
        code = static_cast<int>(*args[0].asInt());
    }
    std::exit(code);
    return Value();
}

EASYSCRIPT_MODULE_INIT {
    s_vm = vm;
    vm->defineModuleNative(mod, "platform", os_platform, 0);
    vm->defineModuleNative(mod, "arch", os_arch, 0);
    vm->defineModuleNative(mod, "getEnv", os_getEnv, 1);
    vm->defineModuleNative(mod, "setEnv", os_setEnv, 2);
    vm->defineModuleNative(mod, "cwd", os_cwd, 0);
    vm->defineModuleNative(mod, "chdir", os_chdir, 1);
    vm->defineModuleNative(mod, "exec", os_exec, 1);
    vm->defineModuleNative(mod, "cpuCount", os_cpuCount, 0);
    vm->defineModuleNative(mod, "exit", os_exit, 1);
    return true;
}
