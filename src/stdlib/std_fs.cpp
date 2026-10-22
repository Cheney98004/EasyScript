#include "easyscript_plugin.hpp"
#include "objects/obj_array.hpp"
#include "objects/obj_map.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

namespace fs = std::filesystem;

static VM* s_vm = nullptr;

static std::string valueToStr(const Value& v) {
    if (v.isObject() && s_vm) {
        auto* str = s_vm->getHeap().getAs<ObjString>(*v.asObject());
        if (str) return str->text;
    }
    return s_vm ? s_vm->valueToString(v) : "";
}

// fs.readText(path: string): string
static Value fs_readText(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string path = valueToStr(args[0]);

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        s_vm->jitRuntimeError("fs.readText: Cannot open file '" + path + "'.");
        return Value();
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    GcHandle handle = s_vm->getHeap().allocateString(std::move(content));
    return Value(handle);
}

// fs.writeText(path: string, content: string): bool
static Value fs_writeText(int argCount, Value* args) {
    if (argCount < 2) return Value(false);
    std::string path = valueToStr(args[0]);
    std::string content = valueToStr(args[1]);

    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return Value(false);

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return Value(bool(file.good()));
}

// fs.appendText(path: string, content: string): bool
static Value fs_appendText(int argCount, Value* args) {
    if (argCount < 2) return Value(false);
    std::string path = valueToStr(args[0]);
    std::string content = valueToStr(args[1]);

    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::app);
    if (!file.is_open()) return Value(false);

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return Value(bool(file.good()));
}

// fs.exists(path: string): bool
static Value fs_exists(int argCount, Value* args) {
    if (argCount < 1) return Value(false);
    std::string path = valueToStr(args[0]);
    std::error_code ec;
    bool exists = fs::exists(path, ec);
    return Value(exists && !ec);
}

// fs.remove(path: string): bool
static Value fs_remove(int argCount, Value* args) {
    if (argCount < 1) return Value(false);
    std::string path = valueToStr(args[0]);
    std::error_code ec;
    bool ok = fs::remove(path, ec);
    return Value(ok && !ec);
}

// fs.removeTree(path: string): int
static Value fs_removeTree(int argCount, Value* args) {
    if (argCount < 1) return Value(int64_t(0));
    std::string path = valueToStr(args[0]);
    std::error_code ec;
    std::uintmax_t count = fs::remove_all(path, ec);
    if (ec) return Value(int64_t(0));
    return Value(static_cast<int64_t>(count));
}

// fs.makeDir(path: string): bool
static Value fs_makeDir(int argCount, Value* args) {
    if (argCount < 1) return Value(false);
    std::string path = valueToStr(args[0]);
    std::error_code ec;
    bool ok = fs::create_directories(path, ec);
    return Value(!ec && (ok || fs::exists(path)));
}

// fs.listDir(path: string): Array<string>
static Value fs_listDir(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string path = valueToStr(args[0]);

    std::vector<Value> result;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        std::string filename = entry.path().filename().string();
        GcHandle h = s_vm->getHeap().allocateString(std::move(filename));
        result.push_back(Value(h));
    }
    GcHandle arrHandle = s_vm->getHeap().allocateArray(std::move(result));
    return Value(arrHandle);
}

// fs.stat(path: string): Map
static Value fs_stat(int argCount, Value* args) {
    if (argCount < 1 || !s_vm) return Value();
    std::string path = valueToStr(args[0]);

    std::error_code ec;
    auto status = fs::status(path, ec);
    if (ec || !fs::exists(status)) return Value();

    bool isDir = fs::is_directory(status);
    bool isFile = fs::is_regular_file(status);
    int64_t fileSize = isFile ? static_cast<int64_t>(fs::file_size(path, ec)) : 0;

    std::unordered_map<Value, Value> mapData;
    mapData[Value(s_vm->getHeap().allocateString("isDir"))] = Value(isDir);
    mapData[Value(s_vm->getHeap().allocateString("isFile"))] = Value(isFile);
    mapData[Value(s_vm->getHeap().allocateString("size"))] = Value(fileSize);

    GcHandle mapHandle = s_vm->getHeap().allocateMap(std::move(mapData));
    return Value(mapHandle);
}

EASYSCRIPT_MODULE_INIT {
    s_vm = vm;
    vm->defineModuleNative(mod, "readText", fs_readText, 1);
    vm->defineModuleNative(mod, "writeText", fs_writeText, 2);
    vm->defineModuleNative(mod, "appendText", fs_appendText, 2);
    vm->defineModuleNative(mod, "exists", fs_exists, 1);
    vm->defineModuleNative(mod, "remove", fs_remove, 1);
    vm->defineModuleNative(mod, "removeTree", fs_removeTree, 1);
    vm->defineModuleNative(mod, "makeDir", fs_makeDir, 1);
    vm->defineModuleNative(mod, "listDir", fs_listDir, 1);
    vm->defineModuleNative(mod, "stat", fs_stat, 1);
    return true;
}
